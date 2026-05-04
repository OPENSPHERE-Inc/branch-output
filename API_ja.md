# Branch Output API リファレンス

[English version](./API.md)

## レコーディングファイル名オーバーライド

### 概要

レコーディングファイル名オーバーライドは、Branch Output の公開されたプロシージャを使って
ストリーム録画およびリプレイバッファ保存のファイル名フォーマットを実行時にオーバーライドできる機能です。

オーバーライドしたファイル名フォーマットは、フィルタープロパティで設定したものとは別に保持され、
プロシージャでリセットするか OBS を終了するまで有効化されます。

主な用途として、現在のシーンやテキストインプットの値、その他の外部データに合わせて
録画ファイル名を実行時にオーバーライドし、録画ファイルを整理した状態で保存するケースが挙げられます。

### スレッド・呼び出しコンテキスト

本 API のすべてのプロシージャは以下のスレッドルールに従います。

- **スレッド安全性**: 任意のスレッドから呼び出し可能です。録画リスタートやファイル分割を伴う場合が
  あるため定数時間の動作は保証されません。レイテンシが重要なホットパスからの呼び出しは避けてください。
- **コールバックからの呼び出し**: デッドロックが発生するため、OBS のシグナルコールバック
  （`obs_source_signal` / `obs_output_signal` 等）やフロントエンドイベントコールバック
  （`obs_frontend_event_callback`）からは呼び出さないでください。スクリプトのタイマーコールバック、
  ホットキーハンドラ、UI イベントハンドラからの呼び出しが安全です。ホットキーコールバックから呼ぶ
  場合は、呼び出し元で Branch Output のロックを保持していないことを確認してください。後述の
  `osi_branch_output_get_filter_list` にも同じ制約が適用されます。
- **遅延反映**: 録画が遷移中（pending、分割、リスタート処理中）の場合、オーバーライドは同期的には
  反映されず、最大 1 秒程度の遅延で適用されます。プロシージャ呼び出しは即座に返却され、反映
  タイミングの通知はありません。

### フィルターソースの取得方法

フィルターごとのオーバーライドプロシージャ（`override_recording_filename_format`,
`override_replay_buffer_filename_format`）は、**個々の Branch Output フィルターソース**に登録されて
います — 親ソース・シーンではありません。呼び出すには親ソースではなくフィルターソース自体への参照が
必要です。

典型的な取得フローは以下のとおりです。

1. グローバルプロシージャ `osi_branch_output_get_filter_list` を呼び出して、現在ロードされている
   すべての Branch Output フィルターの UUID を取得
2. `obs_get_source_by_uuid(filter_uuid)` でフィルターソースへの参照を取得
3. `obs_source_get_proc_handler(filter_source)` でプロシージャハンドラを取得
4. 使用後は必ず `obs_source_release()` でソースを解放

または、親ソースへの参照が既にある場合は `obs_source_get_filter_by_name(parent, filter_name)` を
使用することもできます。

### ストリーム録画ファイル名フォーマットのオーバーライド

Branch Output フィルターソースに登録されたプロシージャで、ストリーム録画の出力ファイル名
フォーマットを実行時にオーバーライドします。

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

# 対象 Branch Output フィルターを UUID から取得。format に "" を渡すとクリア。
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua サンプルコード**

```lua
local obs = obslua

-- 対象 Branch Output フィルターを UUID から取得。format に "" を渡すとクリア。
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end
```

### リプレーバッファー保存ファイル名フォーマットのオーバーライド

Branch Output フィルターソースに登録されたプロシージャで、リプレイバッファ保存時の出力ファイル名
フォーマットを実行時にオーバーライドします。

| 項目 | 内容 |
|------|------|
| プロシージャ名 | `override_replay_buffer_filename_format` |
| シグネチャ | `void override_replay_buffer_filename_format(in string format)` |
| 登録先 | Branch Output フィルターソース（`obs_source_get_proc_handler(filter_source)`） |
| パラメータ | `format` (string) — 新しいファイル名フォーマット。OBS の日時フォーマット（`%CCYY-%MM-%DD %hh-%mm-%ss` など）が使用可能。**空文字列を渡すとオーバーライドがクリアされ、フィルタープロパティで設定された元のファイル名フォーマットに戻ります**。 |
| 戻り値 | なし |

オーバーライドされたファイル名フォーマットは、次回のリプレイバッファ保存時に使用されます。
リプレイバッファが実行中でも即座に反映され、リプレイバッファ自体の再起動は発生しません。

**Python サンプルコード**

```python
import obspython as obs

# 対象 Branch Output フィルターを UUID から取得。format に "" を渡すとクリア。
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua サンプルコード**

```lua
local obs = obslua

-- 対象 Branch Output フィルターを UUID から取得。format に "" を渡すとクリア。
local bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter ~= nil then
    local ph = obs.obs_source_get_proc_handler(bo_filter)
    local cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "Replay %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_replay_buffer_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
end
```

### Branch Output フィルター一覧取得

OBS にロードされている Branch Output フィルターの一覧を取得するためのグローバルプロシージャです。
上記のオーバーライドプロシージャを呼び出すには対象フィルターの UUID が必要なので、このプロシージャで
取得した一覧からユーザーに選択させるのが一般的です。

| 項目 | 内容 |
|------|------|
| プロシージャ名 | `osi_branch_output_get_filter_list` |
| シグネチャ | `void osi_branch_output_get_filter_list(out string json)` |
| 登録先 | グローバルプロシージャハンドラ（`obs_get_proc_handler()`） |
| パラメータ | `json` (out string) — Branch Output フィルター一覧を表す JSON 文字列 |
| 戻り値 | なし |

**注意事項**

- 本プロシージャは `obs_module_post_load()` で登録されます。登録前に呼び出すと
  `proc_handler_call()` は `false` を返し、`out string json` パラメータには何も書き込まれません。
  `json` を読み取る前に必ず戻り値を確認してください。
- **プライベートソース**（OBS フロントエンドに表示されないソース）に適用されたフィルターは、
  ステータスドックの表示ルールと整合させるため意図的に結果から除外されます。
- 返されるリストは呼び出し時点のスナップショットです。フィルターの追加・削除に反応する必要がある
  場合は、定期的にポーリングするか必要に応じてリフレッシュしてください。
- **スレッド安全性**: 任意のスレッドから呼び出し可能です。
- **ライフタイム**: `obs_module_unload()` 以降は呼び出さないでください（動作未定義）。

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

Lua には標準の JSON パーサーがないため、OBS が提供する `obs_data_create_from_json()` を使って
返された JSON 文字列を解析します。

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

各サンプルスクリプトは **Python**（`.py`）版と **Lua**（`.lua`）版の 2 種類がプラグインに同梱されて
います。両者は同じ機能を実装しているため、お好みの言語を選択してください。

- Python スクリプトを使用する場合は、OBS メニューの **Tools → Scripts** で Python Settings が
  正しく設定されている必要があります。
- Lua スクリプトは追加設定不要です — Lua サポートは OBS に組み込まれています。

Windows の場合、スクリプトは OBS インストールパスの
`data\obs-plugins\osi-branch-output\scripts\` にインストールされます。macOS および Linux では、
標準的な OBS プラグインデータディレクトリ規則に従います。

#### 共通のセットアップ

両サンプルスクリプトで共通の手順です。

1. OBS のメニューから Tools → Scripts を開く
2. Scripts ダイアログ下部のプラスボタンをクリック
3. スクリプトファイルを選択（`.py` または `.lua` のどちらか）
4. Description 欄で以下を設定:
   - **Text Source** - テキストインプットを選択
   - **Branch Output Filter** - オーバーライドする Branch Output フィルターを選択
   - **Base Filename Format** - ベースとなるファイル名フォーマット（ファイル名末尾に付与されます）
5. 設定した時点でオーバーライドが有効になります。Script Log で動作状況を確認できます。
6. スクリプトがロードされている間、オーバーライドされたファイル名が優先使用され、フィルター
   プロパティ側の設定は使用されません。
7. 無効化したい場合は Loaded Scripts からゴミ箱ボタンで削除してください。

**Windows + "Read from file" — Lua の既知制約:** テキストソースがファイル読み込みモードの場合、
Lua 版は `io.open()` を使用します。Windows ではパスがシステム ANSI コードページ
（例: 日本語ロケールの CP932）で解釈されるため、コードページ外の文字を含むパスは開けないことが
あります。Python 版は CPython がワイド文字 Windows API を使用するため影響を受けません。Windows で
完全な UTF-8 パス対応が必要な場合は Python 版を使用してください。

#### recording-filename-from-text

テキストインプットの値を読み取ってストリーム録画のファイル名フォーマットに反映します。録画状態
ごとの挙動は上記の[録画中の挙動](#ストリーム録画ファイル名フォーマットのオーバーライド)を
参照してください。

**スロットリング:** テキストソースが短時間に繰り返し変化した際にファイルスプリットや録画リスタートが
頻発しないよう、スクリプトは同一テキスト値あたり最大 30 秒に 1 回までに反映を制限しています。この
ため最大 30 秒の遅延が発生する場合があります。スクリプト先頭の `THROTTLE_SECONDS` 定数で
調整できます。

#### replay-buffer-filename-from-text

テキストインプットの値を読み取ってリプレイバッファー保存時のファイル名フォーマットに反映します。
オーバーライドは次回の保存時に反映され、リプレイバッファー自体は再起動しません。

## obs-websocket vendor request

上記のファイル名オーバーライドおよびフィルター一覧取得機能は、**obs-websocket の vendor request**
としても公開されています。OBS のスクリプト機構を使わず、外部ツールやボット、Stream Deck 連携から
obs-websocket 経由で直接操作できます。

- **ベンダー名 (vendor name):** `osi_branch_output`
- **トランスポート:** obs-websocket 5.x の `CallVendorRequest`
- **必要条件:** obs-websocket がインストールされていること。未インストールの場合は以下のリクエストは利用できません。

### セキュリティ

これらのリクエストの認証は、obs-websocket 自体の設定に完全に従います。プラグイン側で独自の認証層を追
加することはありません。obs-websocket が認証を要求する設定であれば、認証済みクライアントのみが以下の
リクエストを発行できます。obs-websocket が認証なしの設定であれば、obs-websocket エンドポイントに到達
可能な任意のクライアントから発行可能になります。

obs-websocket を `localhost` 以外に公開する場合は、obs-websocket のパスワード認証を有効化したうえで、
`wss://` を終端するリバースプロキシ（例: nginx, Caddy）によって TLS 越しに接続を保護してください。
obs-websocket のパスワードは以下のすべてのリクエストに対するフルアクセス権と等価であり、OBS ユーザー
相当の資格情報として扱ってください。

### 制限事項

- `filter_uuid` は 36 文字のハイフン付き UUID 文字列で、かつ小文字の正規形式
  （8-4-4-4-12、hex は小文字）である必要があります。これは `obs_source_get_uuid()` の出力形式と
  一致します。大文字 hex、ハイフンの欠落・余剰、その他の形式は
  `"filter_uuid must be a lowercase canonical UUID string"` で拒否されます。
- `format` は 1024 バイトを超えられません。超過した場合は `"format too long"` で拒否されます。
- `format` は相対パス表現である必要があります。絶対パス（POSIX の `/...`、Windows の `\...` や
  `X:\...`）および `..` セグメントは `"format must not contain path traversal or absolute paths"`
  で拒否されます。
- `filter_uuid` は Branch Output フィルターソースに解決される必要があります。他種別のソースや
  存在しない UUID は `"UUID does not refer to a Branch Output filter"` で拒否されます。
- 録画遷移中（ファイル分割、リスタート）にリクエストを連続送信すると、遷移完了までリクエストが
  ブロックされることがあります。

### クライアント側スロットリング

プラグインは `override_recording_filename_format` および `override_replay_buffer_filename_format`
に対して**レート制限を行いません**。録画中に `format` が異なるリクエストを受けるたびに、ファイル
分割または録画リスタートが発生する可能性があるため、録画側が落ち着くより速く更新を投げ続ける
クライアントはファイルを断片化させ、エンコーダパイプラインに負荷をかけます。

同梱の OBS スクリプト（[スロットリング](#recording-filename-from-text)を参照）は同一値を重複排除し、
異なる `format` ごとに 30 秒に最大 1 回までの更新に制限しています。vendor request はこのスクリプトを
経由しないため、obs-websocket 経由で操作する場合はこのガードを迂回します。**クライアント側で同等の
スロットリングを実装してください** — 最低限、同一の `format` 値を抑制し、異なる更新もレート制限して
ください（30 秒以上を出発点とし、ファイル分割・リスタートの許容度に合わせて調整してください）。

### `get_filter_list`

現在 OBS にロードされている Branch Output フィルター一覧を列挙します。

| 項目 | 内容 |
|------|------|
| リクエストタイプ | `get_filter_list` |
| リクエストデータ | *(なし)* |
| レスポンス | `{ "success": bool, "error"?: string, "filters": array }` |

`filters` 配列の各要素:

| フィールド | 内容 |
|-----------|------|
| `source_name` | Branch Output フィルターが適用されている親ソース／シーン名 |
| `source_uuid` | 親ソース／シーンの UUID |
| `filter_name` | Branch Output フィルターの名前 |
| `filter_uuid` | Branch Output フィルターの UUID（下記のオーバーライドリクエストで使用） |

**プライベートソース**に適用されたフィルターは、ステータスドックの表示ルールに合わせて結果から除外されます。

### `override_recording_filename_format`

指定した Branch Output フィルターのストリーム録画ファイル名フォーマットをオーバーライドします。

| 項目 | 内容 |
|------|------|
| リクエストタイプ | `override_recording_filename_format` |
| リクエストデータ | `{ "filter_uuid": string, "format": string }` |
| レスポンス | `{ "success": bool, "error"?: string }` |
| オーバーライド解除 | `format` に空文字列を指定 |

`success: true` は「リクエストが受理され、オーバーライド値がフィルターに保存された」ことを意味します。
新しいフォーマットがすでに録画ファイルへ適用されたことを示すものでは**ありません**。
適用タイミングはプロシージャハンドラと同一で、ファイル分割が有効な場合は次の機会にファイル分割が
トリガーされ、無効な場合は録画がリスタートされます。録画が未開始の場合は次回の録画開始時に
新しいフォーマットが使用されます。詳細な挙動は上記の
[ストリーム録画ファイル名フォーマットのオーバーライド](#ストリーム録画ファイル名フォーマットのオーバーライド)
を参照してください。

### `override_replay_buffer_filename_format`

指定した Branch Output フィルターのリプレイバッファ保存ファイル名フォーマットをオーバーライドします。

| 項目 | 内容 |
|------|------|
| リクエストタイプ | `override_replay_buffer_filename_format` |
| リクエストデータ | `{ "filter_uuid": string, "format": string }` |
| レスポンス | `{ "success": bool, "error"?: string }` |
| オーバーライド解除 | `format` に空文字列を指定 |

`success: true` は「リクエストが受理され、オーバーライド値がフィルターに保存された」ことを意味します。
リプレイバッファ自体はリスタートされず、新しいフォーマットは次回のリプレイバッファ保存時に
反映されます。

### 呼び出し例

リクエスト:

```json
{
  "requestType": "CallVendorRequest",
  "requestData": {
    "vendorName": "osi_branch_output",
    "requestType": "override_recording_filename_format",
    "requestData": {
      "filter_uuid": "87654321-4321-4321-4321-cba987654321",
      "format": "%CCYY-%MM-%DD %hh-%mm-%ss MyScene"
    }
  }
}
```

成功時のレスポンス:

```json
{ "success": true }
```

失敗時のレスポンス（UUID 形式不正の場合）:

```json
{ "success": false, "error": "filter_uuid must be a lowercase canonical UUID string" }
```
