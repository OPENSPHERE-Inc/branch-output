# Branch Output API リファレンス

[English version](./API.md)

## レコーディングファイル名オーバーライド

### 概要

レコーディングファイル名オーバーライドは、Branch Output の公開プロシージャを使ってストリーム録画およびリプレイバッファ保存のファイル名フォーマットを実行時にオーバーライドする機能です。

オーバーライドしたフォーマットは Branch Output のプロパティとは別に保持され、
プロシージャでリセットするか OBS を終了するまで有効です。
メインアウトプットでは、メインアウトプットのソースが作り直されたとき（プロファイルの切り替えなど）にも解除されます。

現在のシーンやテキストインプットの値、その他の外部データに応じてファイル名を切り替え、録画ファイルを整理した状態で保存したいというプロダクションの要請で実装されました。

各プロシージャに共通する制約事項（スレッド／コールバック安全性、レイテンシ、遅延反映、登録タイミング、
モジュールアンロード、メインアウトプット）は[既知の制限事項](#既知の制限事項)を参照してください。

### 対象ソースの取得方法

オーバーライドプロシージャ（`override_recording_filename_format`、`override_replay_buffer_filename_format`）は、
**個々の Branch Output のソース**に登録されます。呼び出すには、そのソースへの参照が必要です。

- Branch Output フィルター: フィルターソース自体（親ソース・シーンではありません）。
- メインアウトプット（1.1.0 以降）: メインアウトプットのソース。
  - OBS フロントエンドには表示されないプライベートソースです。

典型的な取得フロー:

1. 後述の一覧取得プロシージャのどちらかで、対象ソースの UUID を取得。
   - `osi_branch_output_get_output_list`（1.1.0 以降）の `output_uuid`: フィルターとメインアウトプット。
   - `osi_branch_output_get_filter_list` の `filter_uuid`: フィルターのみ。
2. `obs_get_source_by_uuid(uuid)` でソース参照を取得。
3. `obs_source_get_proc_handler(source)` でプロシージャハンドラを取得。
4. 使用後は `obs_source_release()` でソースを解放。

フィルターの場合、親ソースへの参照が既にあれば `obs_source_get_filter_by_name(parent, filter_name)` も使用できます。

### ストリーム録画ファイル名フォーマットのオーバーライド

Branch Output のソース（フィルターまたはメインアウトプット）に登録されたプロシージャで、ストリーム録画の出力ファイル名フォーマットを実行時にオーバーライドします。

| 項目 | 内容 |
|------|------|
| プロシージャ名 | `override_recording_filename_format` |
| シグネチャ | `void override_recording_filename_format(in string format)` |
| 登録先 | Branch Output フィルターソース、またはメインアウトプットのソース（1.1.0 以降）（`obs_source_get_proc_handler(source)`） |
| パラメータ | `format` (string) — 新しいファイル名フォーマット。OBS の日時フォーマット（`%CCYY-%MM-%DD %hh-%mm-%ss` など）が使用可能。**空文字列を渡すとオーバーライドがクリアされ、Branch Output のプロパティで設定された元のファイル名フォーマットに戻ります**。 |
| 戻り値 | なし |

録画状態ごとの挙動:

- 録画開始前: 録画開始時にオーバーライドされたフォーマットが使用されます。
- 録画中（ファイル分割有効時）: フォーマットが変化した時点でファイルスプリットが即座に実行されます。
  録画がまだデータを書き込んでいない場合（録画開始直後など）は、データの書き込み後にファイルスプリットが実行されます。
- 録画中（ファイル分割無効時）: 新しいフォーマットで録画がリスタートされます。

**Python サンプルコード**

```python
import obspython as obs

# 対象 Branch Output のソース（フィルターまたはメインアウトプット）を UUID から取得。
# format に "" を渡すとクリア。
bo_source = obs.obs_get_source_by_uuid(target_uuid)
if bo_source:
    ph = obs.obs_source_get_proc_handler(bo_source)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_destroy(cd)
    obs.obs_source_release(bo_source)
```

**Lua サンプルコード**

```lua
local obs = obslua

-- 対象 Branch Output のソース（フィルターまたはメインアウトプット）を UUID から取得。
-- format に "" を渡すとクリア。
local bo_source = obs.obs_get_source_by_uuid(target_uuid)
if bo_source ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_source)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_destroy(cd)
    obs.obs_source_release(bo_source)
end
```

### リプレーバッファー保存ファイル名フォーマットのオーバーライド

Branch Output のソース（フィルターまたはメインアウトプット）に登録されたプロシージャで、リプレイバッファ保存時の出力ファイル名フォーマットを実行時にオーバーライドします。

| 項目 | 内容 |
|------|------|
| プロシージャ名 | `override_replay_buffer_filename_format` |
| シグネチャ | `void override_replay_buffer_filename_format(in string format)` |
| 登録先 | Branch Output フィルターソース、またはメインアウトプットのソース（1.1.0 以降）（`obs_source_get_proc_handler(source)`） |
| パラメータ | `format` (string) — 新しいファイル名フォーマット。OBS の日時フォーマット（`%CCYY-%MM-%DD %hh-%mm-%ss` など）が使用可能。**空文字列を渡すとオーバーライドがクリアされ、Branch Output のプロパティで設定された元のファイル名フォーマットに戻ります**。 |
| 戻り値 | なし |

新しいフォーマットは次回のリプレイバッファ保存時に使用されます。リプレイバッファ自体の再起動は発生しません。

**Python サンプルコード**

```python
import obspython as obs

# 対象 Branch Output のソース（フィルターまたはメインアウトプット）を UUID から取得。
# format に "" を渡すとクリア。
bo_source = obs.obs_get_source_by_uuid(target_uuid)
if bo_source:
    ph = obs.obs_source_get_proc_handler(bo_source)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_destroy(cd)
    obs.obs_source_release(bo_source)
```

**Lua サンプルコード**

```lua
local obs = obslua

-- 対象 Branch Output のソース（フィルターまたはメインアウトプット）を UUID から取得。
-- format に "" を渡すとクリア。
local bo_source = obs.obs_get_source_by_uuid(target_uuid)
if bo_source ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_source)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_destroy(cd)
    obs.obs_source_release(bo_source)
end
```

### Branch Output フィルター一覧取得

OBS にロードされている Branch Output フィルターの一覧を返すグローバルプロシージャです。上記オーバーライドプロシージャに渡すフィルター UUID を取得するために使用します。

メインアウトプットは含まれません。
フィルターとメインアウトプットの両方を得るには、`osi_branch_output_get_output_list`（1.1.0 以降、
[Branch Output 一覧取得](#branch-output-一覧取得)）を使用してください。

| 項目 | 内容 |
|------|------|
| プロシージャ名 | `osi_branch_output_get_filter_list` |
| シグネチャ | `void osi_branch_output_get_filter_list(out string json)` |
| 登録先 | グローバルプロシージャハンドラ（`obs_get_proc_handler()`） |
| パラメータ | `json` (out string) — Branch Output フィルター一覧を表す JSON 文字列 |
| 戻り値 | なし |

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
| `source_name` | 親ソース／シーン名 |
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

    obs.calldata_destroy(cd)
    return filters
```

**Lua サンプルコード**

Lua には標準の JSON パーサーがないため、OBS の `obs_data_create_from_json()` で解析します。

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

    obs.calldata_destroy(cd)
    return filters
end
```

### Branch Output 一覧取得

OBS にロードされている Branch Output（フィルターとメインアウトプットの両方）の一覧を返すグローバルプロシージャです。
1.1.0 以降で使用できます。上記オーバーライドプロシージャに渡す UUID を取得するために使用します。

| 項目 | 内容 |
|------|------|
| プロシージャ名 | `osi_branch_output_get_output_list` |
| シグネチャ | `void osi_branch_output_get_output_list(out string json)` |
| 登録先 | グローバルプロシージャハンドラ（`obs_get_proc_handler()`） |
| パラメータ | `json` (out string) — Branch Output 一覧を表す JSON 文字列 |
| 戻り値 | なし |

**返される JSON の構造**

```json
{
  "outputs": [
    {
      "type": "filter",
      "output_uuid": "87654321-4321-4321-4321-cba987654321",
      "source_name": "Main Scene",
      "source_uuid": "12345678-1234-1234-1234-123456789abc",
      "filter_name": "Branch Output 1",
      "filter_uuid": "87654321-4321-4321-4321-cba987654321"
    },
    {
      "type": "program",
      "output_uuid": "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0",
      "output_name": "Main Output 1"
    },
    ...
  ]
}
```

| フィールド | 現れる `type` | 内容 |
|-----------|---------------|------|
| `type` | すべて | Branch Output フィルターは `filter`、メインアウトプットは `program` |
| `output_uuid` | すべて | オーバーライドプロシージャが登録されたソースの UUID（オーバーライドプロシージャ呼び出しに使用） |
| `source_name` | `filter` | 親ソース／シーン名 |
| `source_uuid` | `filter` | 親ソース／シーンの UUID |
| `filter_name` | `filter` | Branch Output フィルターの名前 |
| `filter_uuid` | `filter` | Branch Output フィルターの UUID（`output_uuid` と同じ値） |
| `output_name` | `program` | メインアウトプットの名前 |

- 今後のバージョンで `type` の値が増えることがあります。知らない `type` の要素は読み飛ばしてください。
- 要素の順序は保証されません。
- 1.1.0 より前のプラグインにはこのプロシージャが登録されていないため、`proc_handler_call()` が `false` を返します。
  その場合は `osi_branch_output_get_filter_list` を使用してください。

**Python サンプルコード**

`(ラベル, UUID)` のタプルの一覧を返します。1.1.0 より前のプラグインでは
`osi_branch_output_get_filter_list` に切り替えます。

```python
import json
import obspython as obs

def call_global_list_proc(proc_name):
    ph = obs.obs_get_proc_handler()
    cd = obs.calldata_create()
    try:
        found = obs.proc_handler_call(ph, proc_name, cd)
        json_str = obs.calldata_string(cd, "json") if found else None
    finally:
        obs.calldata_destroy(cd)
    return found, json_str

def parse_list(json_str, list_key):
    outputs = []
    if not json_str:
        return outputs
    try:
        data = json.loads(json_str)
    except json.JSONDecodeError:
        obs.script_log(obs.LOG_WARNING, "Failed to parse Branch Output list JSON")
        return outputs

    for item in data.get(list_key, []):
        # get_filter_list の要素には "type" がない
        item_type = item.get("type", "") if list_key == "outputs" else "filter"
        if item_type == "filter":
            label = f'{item.get("source_name", "")} / {item.get("filter_name", "")}'
            outputs.append((label, item.get("filter_uuid", "")))
        elif item_type == "program":
            label = f'[Main Output] {item.get("output_name", "")}'
            outputs.append((label, item.get("output_uuid", "")))
        # それ以外の type の要素は読み飛ばす
    return outputs

def get_branch_outputs():
    found, json_str = call_global_list_proc("osi_branch_output_get_output_list")
    if found:
        return parse_list(json_str, "outputs")
    # 1.1.0 より前のプラグイン: フィルターのみ
    found, json_str = call_global_list_proc("osi_branch_output_get_filter_list")
    if found:
        return parse_list(json_str, "filters")
    return []
```

**Lua サンプルコード**

`{label = ..., uuid = ...}` のテーブルの一覧を返します。1.1.0 より前のプラグインでは
`osi_branch_output_get_filter_list` に切り替えます。

```lua
local obs = obslua

local function call_global_list_proc(proc_name)
    local ph = obs.obs_get_proc_handler()
    local cd = obs.calldata_create()
    local found = obs.proc_handler_call(ph, proc_name, cd)
    local json_str = nil
    if found then
        json_str = obs.calldata_string(cd, "json")
    end
    obs.calldata_destroy(cd)
    return found, json_str
end

local function parse_list(json_str, list_key)
    local outputs = {}
    if json_str == nil or json_str == "" then
        return outputs
    end
    local data = obs.obs_data_create_from_json(json_str)
    if data == nil then
        obs.script_log(obs.LOG_WARNING, "Failed to parse Branch Output list JSON")
        return outputs
    end

    local array = obs.obs_data_get_array(data, list_key)
    local count = obs.obs_data_array_count(array)
    for i = 0, count - 1 do
        local item = obs.obs_data_array_item(array, i)
        -- get_filter_list の要素には "type" がない
        local item_type = "filter"
        if list_key == "outputs" then
            item_type = obs.obs_data_get_string(item, "type")
        end
        if item_type == "filter" then
            table.insert(outputs, {
                label = obs.obs_data_get_string(item, "source_name") .. " / "
                    .. obs.obs_data_get_string(item, "filter_name"),
                uuid = obs.obs_data_get_string(item, "filter_uuid"),
            })
        elseif item_type == "program" then
            table.insert(outputs, {
                label = "[Main Output] " .. obs.obs_data_get_string(item, "output_name"),
                uuid = obs.obs_data_get_string(item, "output_uuid"),
            })
        end
        -- それ以外の type の要素は読み飛ばす
        obs.obs_data_release(item)
    end
    obs.obs_data_array_release(array)
    obs.obs_data_release(data)
    return outputs
end

function get_branch_outputs()
    local found, json_str = call_global_list_proc("osi_branch_output_get_output_list")
    if found then
        return parse_list(json_str, "outputs")
    end
    -- 1.1.0 より前のプラグイン: フィルターのみ
    found, json_str = call_global_list_proc("osi_branch_output_get_filter_list")
    if found then
        return parse_list(json_str, "filters")
    end
    return {}
end
```

### サンプルスクリプトの使い方

各サンプルは **Python**（`.py`）版と **Lua**（`.lua`）版の 2 種類が同梱されています。両者は同じ機能を実装しており、お好みの言語を選択してください。

- Python スクリプトを使用する場合、OBS の **Tools → Scripts** で Python Settings の設定が必要です。
- Lua スクリプトは追加設定不要です。

インストールパス: Windows では OBS インストールパスの `data\obs-plugins\osi-branch-output\scripts\`、macOS / Linux では標準の OBS プラグインデータディレクトリ。

#### 共通のセットアップ

1. OBS メニュー → **Tools → Scripts**。
2. ダイアログ下部の **+** ボタンをクリック。
3. スクリプトファイルを選択（`.py` / `.lua` のどちらか）。
4. Description 欄で以下を設定:
   - **Text Source** — 読み取るテキストインプット。
   - **Branch Output** — オーバーライド対象のフィルターまたはメインアウトプット。
     - フィルターは `<ソース名> / <フィルター名>`、メインアウトプットは `[Main Output] <出力名>` と表示されます。
     - 1.1.0 より前のプラグインでは、フィルターだけが並びます。
   - **Base Filename Format** — ファイル名末尾に付与されるベースフォーマット。
5. 設定した時点でオーバーライドが有効になります。**Script Log** で動作状況を確認できます。
6. ロード中はスクリプトのオーバーライドが Branch Output のプロパティ側の設定より優先されます。
   - メインアウトプットでは、メインアウトプットのソースが作り直された後（プロファイルの切り替えなど）に
     オーバーライドを適用し直し、OBS の起動時はメインアウトプットが読み込まれるまで待ちます。
7. ゴミ箱ボタンでスクリプトを削除すると無効化されます。

#### recording-filename-from-text

テキストインプットの値を読み取ってストリーム録画のファイル名フォーマットに反映します。録画状態ごとの挙動は[こちら](#ストリーム録画ファイル名フォーマットのオーバーライド)を参照してください。

短時間の頻繁な変化でファイルスプリットや録画リスタートが頻発しないよう、同一テキスト値あたり 30 秒に 1 回までに反映を制限しています。スクリプト先頭の `THROTTLE_SECONDS` 定数で調整可能です。

#### replay-buffer-filename-from-text

テキストインプットの値を読み取ってリプレイバッファー保存時のファイル名フォーマットに反映します。次回保存時に反映され、リプレイバッファー自体は再起動しません。

## 既知の制限事項

以下は本ドキュメントで扱うすべてのプロシージャおよびサンプルスクリプトに共通する制約です。

- **スレッド安全性**: 任意のスレッドから呼び出し可能です。
- **コールバックからの呼び出し**: デッドロックの恐れがあるため、OBS のシグナルコールバック（`obs_source_signal` / `obs_output_signal` 等）やフロントエンドイベントコールバック（`obs_frontend_event_callback`）からは呼び出さないでください。スクリプトのタイマー、ホットキーハンドラ、UI イベントハンドラからの呼び出しが安全です。ホットキーコールバックから呼ぶ場合は、呼び出し元で Branch Output のロックを保持していないことを確認してください。
- **レイテンシ**: 録画リスタートやファイル分割を伴う場合があるため定数時間の動作は保証されません。レイテンシが重要なホットパスからの呼び出しは避けてください。
- **遅延反映**: 録画が遷移中（開始直後、pending、分割、リスタート処理中）または一時停止中の場合、オーバーライドは同期的には反映されず、後から適用されます。
  通常は 1 秒程度で適用されますが、録画開始直後は最初のデータが書き込まれた後に、一時停止中は一時停止の解除後に適用されます。
  プロシージャ呼び出しは即座に返却され、反映タイミングの通知はありません。
- **登録タイミング**: `osi_branch_output_get_filter_list` と `osi_branch_output_get_output_list` は
  `obs_module_post_load()` で登録されます。それ以前の呼び出しは `proc_handler_call()` が `false` を返し、
  `out` パラメータには何も書き込まれません。読み取り前に必ず戻り値を確認してください。
- **モジュールアンロード**: `obs_module_unload()` 以降はどのプロシージャも呼び出さないでください（動作未定義）。
- **プライベートソースは除外**: 2 つの一覧取得プロシージャは、ステータスドックの表示ルールと整合させるため、
  OBS フロントエンドに表示されないソース上のフィルターを意図的に結果から除外します。
  メインアウトプットはプライベートソースで実装されていますが、`osi_branch_output_get_output_list` には含まれます。
- **スナップショット**: 一覧は呼び出し時点のスナップショットです。追加・削除に追従するには定期的にポーリングするか、
  必要に応じてリフレッシュしてください。シーンコレクションやプロファイルの切り替え中は、出力が一時的に一覧から外れることがあります。
- **メインアウトプット**:
  - オーバーライドはメインアウトプットのソースのインスタンスに属し、ソースが作り直される（プロファイルの切り替えなど）と解除されます。
  - 作り直しの前後で UUID が変わらないことも、プロファイルを切り替えてもソースが保持されることもあります。
    インスタンスの入れ替わりを（弱参照などで）検出して、オーバーライドを適用し直してください。
  - イベントだけを手がかりにオーバーライドを送り直すと、録画中の出力を分割・再起動させることがあります。
    同梱のサンプルスクリプトは弱参照で入れ替わりを検出します。
  - メインアウトプットは `OBS_FRONTEND_EVENT_FINISHED_LOADING` で読み込まれます。
    シーンコレクションと一緒に読み込まれたスクリプトからは、OBS の起動直後にはメインアウトプットが見つからないことがあります。
    同梱のサンプルスクリプトは見つかるまで待ちます。
  - シーンコレクションを複製すると、メインアウトプットの UUID も振り直されます。
    ほかのシーンコレクションに保存したスクリプトで選んだメインアウトプットは見つからなくなるため、選び直す必要があります。
- **Windows + "Read from file" — Lua サンプルのみ**: テキストソースがファイル読み込みモードのとき、Lua サンプルは `io.open()` を使用するため、Windows ではパスがシステム ANSI コードページ（例: 日本語ロケールの CP932）で解釈されます。コードページ外の文字を含むパスは開けない場合があります。オープン失敗時、スクリプトはオーバーライドをクリアし `Failed to read text file` 警告を OBS Script Log に書き込みます。Python サンプルは CPython がワイド文字 Windows API を使用するため影響を受けません。Windows で完全な UTF-8 パス対応が必要な場合は Python サンプルを使用してください。
