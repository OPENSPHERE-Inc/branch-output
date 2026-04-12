# Branch Output API リファレンス

[English version](./API.md)

## レコーディングファイル名オーバーライド

### 概要

レコーディングファイル名オーバーライドは、Branch Output の公開されたプロシージャを使ってストリーム録画およびリプレイバッファ保存のファイル名フォーマットをオーバーライドすることができる機能です。

オーバーライドしたファイル名フォーマットは、フィルタープロパティで設定したものとは別に保持され、プロシージャでリセットするか OBS を終了するまで有効化されます。

この機能は、たとえば現在のシーンやテキストインプットの値、その他の外部データによってファイル名をオーバーライドすることで、録画ファイルを整理した状態で保存したいというプロダクションの要請で実装されました。

### スレッド・呼び出しコンテキスト

本 API のすべてのプロシージャは以下のスレッドルールに従います。

- **スレッド安全性**: オーバーライドプロシージャは任意のスレッドから呼び出し可能です。内部的にフィルターの `outputMutex` を取得し、現在の録画状態に応じて `obs_output_update()` の呼び出し（ファイル分割が有効な場合）や、次の interval タイマーティックでの録画リスタートへの遅延処理を行います。このためプロシージャ呼び出しは定数時間であることを保証しません。レイテンシが重要なホットパスからの呼び出しは避けてください。
- **コールバックからの呼び出し**: これらのプロシージャは、**OBS のシグナルコールバック（`obs_source_signal` / `obs_output_signal` 等）やフロントエンドイベントコールバック（`obs_frontend_event_callback`）から呼び出してはいけません**。これらのコールバックは既にフィルターの `outputMutex` や libobs の出力ロックを保持しているか、間接的に取得する可能性があり、オーバーライドプロシージャ内部で取得するロックとの間でデッドロックが発生します。スクリプトのタイマーコールバック、ホットキーハンドラ、UI イベントハンドラからの呼び出しを推奨します。なお、ここで言う「ホットキーハンドラ」は **`obslua` / `obspython` で登録したスクリプト側のホットキーコールバック**を想定しています（スクリプトホットキーは Branch Output 側のロックを保持しない安全なコンテキストからディスパッチされます）。これに対し、**ネイティブプラグインが `obs_hotkey_register_*` で登録したホットキーコールバック**から呼び出す場合は、ディスパッチ元のスレッドが Branch Output とは無関係なロックを既に保持している可能性があります。呼び出し側が `outputMutex` や Qt UI スレッドが必要とするロックを保持していないことを確認してください。この制約は後述の `osi_branch_output_get_filter_list` にも同様に該当します — 同プロシージャは Qt UI スレッドに対してブロッキングキューイング接続で同期するため、UI スレッドが待っているロックを呼び出し側が保持しているとデッドロックを引き起こします。
- **pending 状態中の遅延反映**: 録画が `recordingPending` 状態、またはスプリット／リスタート処理中の場合、オーバーライドは保存され、内部の 1秒間隔タイマー（`intervalTimer`）経由で次のティックで反映されます。プロシージャ呼び出し自体は即座に返却しますが、新しいファイル名フォーマットがいつ有効になったかを示す通知はありません。

### フィルターソースの取得方法

フィルターごとのオーバーライドプロシージャ（`override_recording_filename_format`, `override_replay_buffer_filename_format`）は、**個々の Branch Output フィルターソース**に登録されています — 親ソース・シーンではありません。呼び出すには親ソースではなくフィルターソース自体への参照が必要です。

典型的な取得フローは以下のとおりです。

1. グローバルプロシージャ `osi_branch_output_get_filter_list` を呼び出して、現在ロードされているすべての Branch Output フィルターの UUID を取得
2. `obs_get_source_by_uuid(filter_uuid)` でフィルターソースへの参照を取得
3. `obs_source_get_proc_handler(filter_source)` でプロシージャハンドラを取得
4. 使用後は必ず `obs_source_release()` でソースを解放

または、親ソースへの参照が既にある場合は `obs_source_get_filter_by_name(parent, filter_name)` を使用することもできます。

### ストリーム録画ファイル名フォーマットのオーバーライド

Branch Output フィルターソースに登録されたプロシージャで、ストリーム録画の出力ファイル名フォーマットを実行時にオーバーライドします。

| 項目 | 内容 |
|------|------|
| プロシージャ名 | `override_recording_filename_format` |
| シグネチャ | `void override_recording_filename_format(in string format)` |
| 登録先 | Branch Output フィルターソース（`obs_source_get_proc_handler(filter_source)`） |
| パラメータ | `format` (string) — 新しいファイル名フォーマット。OBS の日時フォーマット（`%CCYY-%MM-%DD %hh-%mm-%ss` など）が使用可能。**空文字列を渡すとオーバーライドがクリアされ、フィルタープロパティで設定された元のファイル名フォーマットに戻ります**。 |
| 戻り値 | なし |

**録画中の挙動**

- 録画開始前: 録画開始時にオーバーライドされたファイル名フォーマットが使用されます
- 録画中（ファイル分割有効時）: ファイル名フォーマットが変化した場合、ファイルスプリットが即座に実行されます
- 録画中（ファイル分割無効時）: 新しいファイル名フォーマットで、録画がリスタートされます

**Python サンプルコード**

```python
import obspython as obs

# 対象 Branch Output フィルターを UUID から取得
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)

# オーバーライドをクリア（空文字列を渡す）
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua サンプルコード**

```lua
local obs = obslua

-- 対象 Branch Output フィルターを UUID から取得
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end

-- オーバーライドをクリア（空文字列を渡す）
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end
```

### リプレーバッファー保存ファイル名フォーマットのオーバーライド

Branch Output フィルターソースに登録されたプロシージャで、リプレイバッファ保存時の出力ファイル名フォーマットを実行時にオーバーライドします。

| 項目 | 内容 |
|------|------|
| プロシージャ名 | `override_replay_buffer_filename_format` |
| シグネチャ | `void override_replay_buffer_filename_format(in string format)` |
| 登録先 | Branch Output フィルターソース（`obs_source_get_proc_handler(filter_source)`） |
| パラメータ | `format` (string) — 新しいファイル名フォーマット。OBS の日時フォーマット（`%CCYY-%MM-%DD %hh-%mm-%ss` など）が使用可能。**空文字列を渡すとオーバーライドがクリアされ、フィルタープロパティで設定された元のファイル名フォーマットに戻ります**。 |
| 戻り値 | なし |

オーバーライドされたファイル名フォーマットは、次回のリプレイバッファ保存時に使用されます。リプレイバッファが実行中でも即座に反映され、リプレイバッファ自体の再起動は発生しません。

**Python サンプルコード**

```python
import obspython as obs

# 対象 Branch Output フィルターを UUID から取得
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)

# オーバーライドをクリア（空文字列を渡す）
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua サンプルコード**

```lua
local obs = obslua

-- 対象 Branch Output フィルターを UUID から取得
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end

-- オーバーライドをクリア（空文字列を渡す）
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end
```

### Branch Output フィルター一覧取得

OBS にロードされている Branch Output フィルターの一覧を取得するためのグローバルプロシージャです。上記のオーバーライドプロシージャを呼び出すには対象フィルターの UUID が必要なので、このプロシージャで取得した一覧からユーザーに選択させるのが一般的です。

| 項目 | 内容 |
|------|------|
| プロシージャ名 | `osi_branch_output_get_filter_list` |
| シグネチャ | `void osi_branch_output_get_filter_list(out string json)` |
| 登録先 | グローバルプロシージャハンドラ（`obs_get_proc_handler()`） |
| パラメータ | `json` (out string) — Branch Output フィルター一覧を表す JSON 文字列 |
| 戻り値 | なし |

**注意事項**

- このプロシージャは `obs_module_post_load()` で登録されます。それ以前（OBS Studio モジュールロード初期段階など）に呼び出すと空のリストが返ります。
- **プライベートソース**（OBS フロントエンドに表示されないソース）に適用されたフィルターは、ステータスドックの表示ルールと整合させるため意図的に結果から除外されます。
- 返されるリストは呼び出し時点でのスナップショットです。フィルターの追加・削除に反応する必要がある場合は、定期的にポーリングするか、必要に応じてリフレッシュしてください。
- **スレッド安全性**: 本プロシージャは任意のスレッドから呼び出し可能です。内部実装ではステータスドックのフィルターテーブルを参照しますが、これは Qt UI スレッドからのみアクセスする必要があります。UI スレッド以外（例: obs-websocket のワーカースレッド）から呼ばれた場合はブロッキングキュー接続で UI スレッドにディスパッチして読み取ります。UI スレッド自身（例: フロントエンドプラグインやホットキーハンドラ）から呼ばれた場合は自己デッドロックを避けるため直接読み取ります。呼び出し側はこの違いを意識する必要はありません。

**返される JSON の構造**

```json
{
  "filters": [
    {
      "source_name": "Main Scene",
      "source_uuid": "12345678-1234-1234-1234-123456789abc",
      "filter_name": "Branch Output 1",
      "filter_uuid": "87654321-4321-4321-4321-cba987654321"
    },
    ...
  ]
}
```

| フィールド | 内容 |
|-----------|------|
| `source_name` | Branch Output フィルターが適用されている親ソース／シーン名 |
| `source_uuid` | 親ソース／シーンの UUID |
| `filter_name` | Branch Output フィルターの名前 |
| `filter_uuid` | Branch Output フィルターの UUID（オーバーライドプロシージャ呼び出しに使用） |

**Python サンプルコード**

```python
import json
import obspython as obs

def get_branch_output_filters():
    filters = []
    ph = obs.obs_get_proc_handler()
    cd = obs.calldata_create()

    if obs.proc_handler_call(ph, "osi_branch_output_get_filter_list", cd):
        json_str = obs.calldata_string(cd, "json")
        if json_str:
            try:
                data = json.loads(json_str)
                for item in data.get("filters", []):
                    filters.append((
                        item.get("source_name", ""),
                        item.get("source_uuid", ""),
                        item.get("filter_name", ""),
                        item.get("filter_uuid", ""),
                    ))
            except json.JSONDecodeError:
                obs.script_log(obs.LOG_WARNING, "Failed to parse filter list JSON")

    obs.calldata_free(cd)
    return filters
```

**Lua サンプルコード**

Lua には標準の JSON パーサーがないため、OBS が提供する `obs_data_create_from_json()` を使って返された JSON 文字列を解析します。

```lua
local obs = obslua

function get_branch_output_filters()
    local filters = {}
    local ph = obs.obs_get_proc_handler()
    local cd = obs.calldata_create()

    if obs.proc_handler_call(ph, "osi_branch_output_get_filter_list", cd) then
        local json_str = obs.calldata_string(cd, "json")
        if json_str and json_str ~= "" then
            local data = obs.obs_data_create_from_json(json_str)
            local array = obs.obs_data_get_array(data, "filters")
            local count = obs.obs_data_array_count(array)
            for i = 0, count - 1 do
                local item = obs.obs_data_array_item(array, i)
                table.insert(filters, {
                    source_name = obs.obs_data_get_string(item, "source_name"),
                    source_uuid = obs.obs_data_get_string(item, "source_uuid"),
                    filter_name = obs.obs_data_get_string(item, "filter_name"),
                    filter_uuid = obs.obs_data_get_string(item, "filter_uuid"),
                })
                obs.obs_data_release(item)
            end
            obs.obs_data_array_release(array)
            obs.obs_data_release(data)
        end
    end

    obs.calldata_free(cd)
    return filters
end
```

### サンプルスクリプトの使い方

各サンプルスクリプトは **Python**（`.py`）版と **Lua**（`.lua`）版の 2 種類がプラグインに同梱されています。両者は同じ機能を実装しているため、お好みの言語を選択してください。

- Python スクリプトを使用する場合は、OBS メニューの **Tools → Scripts** で Python Settings が正しく設定されている必要があります。
- Lua スクリプトは追加設定不要です — Lua サポートは OBS に組み込まれています。

Windows の場合、スクリプトは OBS インストールパスの `data\obs-plugins\osi-branch-output\scripts\` にインストールされます。macOS および Linux では、標準的な OBS プラグインデータディレクトリ規則に従います。

#### recording-filename-from-text.py / recording-filename-from-text.lua

テキストインプットの値を読み取って、ストリーム録画ファイル名フォーマットに反映するスクリプトです。

1. OBS のメニューから Tools → Scripts を開く
2. Scripts ダイアログ下部のプラスボタンをクリック
3. スクリプトファイルを選択（`.py` または `.lua` のどちらか）
4. Loaded Scripts でスクリプトを選択すると、Description で各種設定が行えます。
   - **Text Source** - テキストインプットを選択
   - **Branch Output Filter** - オーバーライドする Branch Output フィルターを選択
   - **Base Filename Format** - ベースとなるファイル名フォーマットを指定。これらのフォーマットはファイル名の末尾に付与されます。
5. 設定を行った時点でオーバーライドが有効です。Script Log をクリックするとスクリプトの動作状況を確認できます。
   例： `[recording-filename-from-text.lua] Recording filename format updated: test %CCYY-%MM-%DD %hh-%mm-%ss`
6. オーバーライド有効の状態で録画するとファイル名はオーバーライドされたものが優先使用されます。
7. オーバーライドを無効化したい場合はスクリプトをゴミ箱ボタンで Loaded Scripts から削除してください。

> **録画中の挙動**
>
> - 録画開始前: 録画開始時にオーバーライドされたファイル名フォーマットが使用されます
> - 録画中（ファイル分割有効時）: ファイル名フォーマットが変化した場合、ファイルスプリットが即座に実行されます
> - 録画中（ファイル分割無効時）: 新しいファイル名フォーマットで、録画がリスタートされます

**注意:** オーバーライドが有効な状態で、フィルタープロパティ設定のファイル名は使用されません。

**スロットリング:** テキストソースが短時間に繰り返し変化した際にファイルスプリットや録画リスタートが頻発しないよう、サンプルスクリプトは同一テキスト値あたり最大 30 秒に 1 回までに反映を制限しています。このため、テキストソースの変更が録画ファイル名に反映されるまで最大 30 秒の遅延が発生する場合があります。スクリプト先頭の `THROTTLE_SECONDS` 定数を編集することでこの間隔を調整できます。

**Windows + "Read from file" — Lua の既知制約:** テキストソースをファイル読み込みモードに設定した場合、Lua 版は `io.open()` を使用します。Windows では `io.open()` は CRT の `fopen()` を呼び出し、パスをシステムの ANSI コードページ（例: 日本語ロケールでは CP932）で解釈するため、そのコードページで表現できない文字を含むパスは Lua スクリプトから開けないことがあります。Python 版は CPython が内部でワイド文字 Windows API を使用するため影響を受けません。Windows で完全な UTF-8 パス対応が必要な場合は Python 版を使用してください。

#### replay-buffer-filename-from-text.py / replay-buffer-filename-from-text.lua

テキストインプットの値を読み取って、リプレイバッファー保存ファイル名フォーマットに反映するスクリプトです。

1. OBS のメニューから Tools → Scripts を開く
2. Scripts ダイアログ下部のプラスボタンをクリック
3. スクリプトファイルを選択（`.py` または `.lua` のどちらか）
4. Loaded Scripts でスクリプトを選択すると、Description で各種設定が行えます。
   - **Text Source** - テキストインプットを選択
   - **Branch Output Filter** - オーバーライドする Branch Output フィルターを選択
   - **Base Filename Format** - ベースとなるファイル名フォーマットを指定。これらのフォーマットはファイル名の末尾に付与されます。
5. 設定を行った時点でオーバーライドが有効です。Script Log をクリックするとスクリプトの動作状況を確認できます。
   例： `[replay-buffer-filename-from-text.lua] Replay buffer filename format updated: test %CCYY-%MM-%DD %hh-%mm-%ss`
6. オーバーライド有効の状態で保存するとファイル名はオーバーライドされたものが優先使用されます。
7. オーバーライドを無効化したい場合はスクリプトをゴミ箱ボタンで Loaded Scripts から削除してください。

**注意:** オーバーライドが有効な状態で、フィルタープロパティ設定のファイル名は使用されません。

**Windows + "Read from file" — Lua の既知制約:** `recording-filename-from-text.lua` で述べた制約がこちらにも同様に該当します。ファイル読み込みモードのパスに非 ANSI 文字が含まれる場合、Lua 版は Windows でファイルを開けないことがあります。Windows で完全な UTF-8 パス対応が必要な場合は Python 版を使用してください。
