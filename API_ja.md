# Branch Output API リファレンス

[English version](./API.md)

## 概要

Branch Output は、**録画 / リプレイバッファのファイル名フォーマットをフィルター単位で実行時に
オーバーライドする** ための小さな公開 API を提供します。利用できるトランスポートは 2 種類です。

1. **proc handler** — プロセス内呼び出し用（OBS Script、プラグインなど）。
2. **obs-websocket vendor request** — プロセス外呼び出し用（外部ツール、bot、Stream Deck 連携など）。
   obs-websocket のインストールが必要。

どちらのトランスポートも、同じ 3 種類の操作を提供します。

| 操作 | 用途 |
|------|------|
| フィルター一覧取得 | 現在ロードされている Branch Output フィルターを列挙する（UUID を返す） |
| 録画ファイル名フォーマットのオーバーライド | フィルター単位。ストリーム録画に作用 |
| リプレイバッファファイル名フォーマットのオーバーライド | フィルター単位。リプレイバッファ保存に作用 |

オーバーライド値はフィルター自身のプロパティ設定とは別に保持され、空文字列を渡してクリアするか
OBS が終了するまで有効です。

典型的なユースケース: 現在のシーン名やテキスト入力の値、その他の外部状態に応じて録画ファイルを
整理する用途。

## proc handler API

### プロシージャ一覧

| プロシージャ名 | 登録先 | シグネチャ |
|----------------|--------|------------|
| `osi_branch_output_get_filter_list` | グローバル（`obs_get_proc_handler()`） | `(out string json)` |
| `override_recording_filename_format` | フィルターソース（`obs_source_get_proc_handler(filter_source)`） | `(in string format)` |
| `override_replay_buffer_filename_format` | フィルターソース（`obs_source_get_proc_handler(filter_source)`） | `(in string format)` |

両方のオーバーライドプロシージャで、`format` に空文字列を渡すとオーバーライドはクリアされ、
フィルタープロパティで設定された値に戻ります。`format` 文字列は OBS の日時指定子
（例: `%CCYY-%MM-%DD %hh-%mm-%ss`）をサポートします。

### フィルターソースの取得

オーバーライドプロシージャは個々の Branch Output フィルターに登録されており、親ソースには
登録されていません。典型的なフローは次の通りです。

1. `osi_branch_output_get_filter_list` を呼び出し、ロード済みの全 Branch Output フィルターの
   UUID を取得する。
2. `obs_get_source_by_uuid(filter_uuid)` → フィルターソースの参照を得る。
3. `obs_source_get_proc_handler(filter_source)` → proc handler を得る。
4. 使用後は `obs_source_release()` でソースを解放する。

親ソースの参照を既に保持している場合は、`obs_source_get_filter_by_name(parent, filter_name)`
を代わりに使うこともできます。

### `osi_branch_output_get_filter_list` が返す JSON

```json
{
  "filters": [
    {
      "source_name": "Main Scene",
      "source_uuid": "12345678-1234-1234-1234-123456789abc",
      "filter_name": "Branch Output 1",
      "filter_uuid": "87654321-4321-4321-4321-cba987654321"
    }
  ]
}
```

| フィールド | 説明 |
|------------|------|
| `source_name` | 親ソース / シーン名 |
| `source_uuid` | 親ソース / シーンの UUID |
| `filter_name` | Branch Output フィルター名 |
| `filter_uuid` | Branch Output フィルターの UUID（オーバーライドプロシージャで使用） |

プライベートソース（OBS フロントエンドに表示されないソース）上のフィルターは Status Dock の
表示ルールに合わせて除外されます。一覧は呼び出し時点のスナップショットなので、フィルターの
追加 / 削除に追従したい場合はポーリングするか必要時に再取得してください。

### `override_recording_filename_format` の挙動

| 録画状態 | 挙動 |
|----------|------|
| 未開始 | 次回の録画開始時にフォーマットが適用される |
| 録画中、ファイル分割が有効 | 即座にファイル分割をトリガーする |
| 録画中、ファイル分割が無効 | 録画をリスタートする |

### `override_replay_buffer_filename_format` の挙動

新しいフォーマットは次回のリプレイバッファ保存時に使用されます。リプレイバッファ自体は
現在動作中であっても **リスタートされません**。

### サンプルコード: 録画ファイル名フォーマットのオーバーライド

**Python**

```python
import obspython as obs

# Pass "" as format to clear the override.
bo_filter = obs.obs_get_source_by_uuid(filter_uuid)
if bo_filter:
    ph = obs.obs_source_get_proc_handler(bo_filter)
    cd = obs.calldata_create()
    obs.calldata_set_string(cd, "format", "MyShow %CCYY-%MM-%DD %hh-%mm-%ss")
    obs.proc_handler_call(ph, "override_recording_filename_format", cd)
    obs.calldata_free(cd)
    obs.obs_source_release(bo_filter)
```

**Lua**

```lua
local obs = obslua

-- Pass "" as format to clear the override.
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

リプレイバッファ向けには、プロシージャ名を `override_replay_buffer_filename_format` に
差し替えてください。

### サンプルコード: フィルター一覧取得

**Python**

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

**Lua** — Lua には組み込みの JSON パーサがないため、OBS の `obs_data_create_from_json()` を
使用します。

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

### 同梱サンプルスクリプト

すぐに使える OBS Script を 2 種類、Python（`.py`）と Lua（`.lua`）の両方で同梱しています。
両バリアントは同じロジックを実装しているので、環境に合った言語を選んでください。Python は
ツール → スクリプト → Python 設定の構成が必要です。Lua は追加設定なしで動作します。

インストールパス:

- Windows: OBS インストールディレクトリ配下の
  `data\obs-plugins\osi-branch-output\scripts\`
- macOS / Linux: 標準の OBS プラグインデータディレクトリ

セットアップ:

1. OBS → ツール → スクリプト → `+` をクリック
2. `.py` または `.lua` バリアントを選択
3. 説明パネルで以下を設定:
   - **Text Source** — 読み取り対象のテキスト入力
   - **Branch Output Filter** — オーバーライド対象のフィルター
   - **Base Filename Format** — ファイル名末尾に付加される文字列
4. オーバーライドは即座に適用される。動作はスクリプトログで確認可能。
5. スクリプトがロードされている間、オーバーライドはフィルター自身の設定より優先される。
6. ゴミ箱ボタンでスクリプトを削除すると、フィルター自身の設定に戻る。

| スクリプト | 読み取り元 | 適用先 |
|------------|-----------|--------|
| `recording-filename-from-text` | テキスト入力の値 | ストリーム録画のファイル名フォーマット |
| `replay-buffer-filename-from-text` | テキスト入力の値 | リプレイバッファ保存のファイル名フォーマット |

録画バリアントは、ファイル分割や録画リスタートの嵐を防ぐために **同一値あたり 30 秒に 1 回**
だけ適用されるようスロットリングしています。スクリプト先頭の `THROTTLE_SECONDS` を編集して
調整できます。リプレイバッファバリアントはバッファをリスタートしないため、スロットリングは
ありません。

## obs-websocket vendor request

同じ 3 つの操作は obs-websocket 5.x の vendor request としても公開されており、プロセス内
スクリプトを使わずに外部ツールからプラグインを操作できます。

- **ベンダー名:** `osi_branch_output`
- **トランスポート:** `CallVendorRequest`
- **要件:** obs-websocket がインストールされていること。

### セキュリティ

認証は obs-websocket に完全に委譲しています。プラグイン側に独自の認証層は追加しません。
obs-websocket を `localhost` 以外に公開する場合は、obs-websocket のパスワード認証を有効化し、
nginx や Caddy などで `wss://` を終端して TLS 越しに公開してください。obs-websocket のパスワードは
OBS のユーザ権限と同等の認証情報として扱う必要があります — それを知っていれば下記すべての
リクエストにフルアクセスできます。

### リクエスト一覧

#### `get_filter_list`

ロードされている Branch Output フィルターをすべて列挙します。

| 項目 | 値 |
|------|----|
| リクエスト種別 | `get_filter_list` |
| リクエストデータ | *(なし)* |
| レスポンス | `{ "success": bool, "error"?: string, "filters": array }` |

`filters[]` 要素:

| フィールド | 説明 |
|------------|------|
| `source_name` | 親ソース / シーン名 |
| `source_uuid` | 親ソース / シーンの UUID |
| `filter_name` | Branch Output フィルター名 |
| `filter_uuid` | Branch Output フィルターの UUID（後述のオーバーライドリクエストで使用） |

プライベートソース上のフィルターは除外されます。

#### `override_recording_filename_format` / `override_replay_buffer_filename_format`

特定の Branch Output フィルターのファイル名フォーマットをオーバーライドします。

| 項目 | 値 |
|------|----|
| リクエスト種別 | `override_recording_filename_format` または `override_replay_buffer_filename_format` |
| リクエストデータ | `{ "filter_uuid": string, "format": string }` |
| レスポンス | `{ "success": bool, "error"?: string }` |
| オーバーライドのクリア | `format` に `""` を渡す |

`success: true` は値がフィルターに **保存された** ことを意味します。新しいフォーマットが
録画ファイルへ既に適用されたことを示すものでは **ありません**（上の
[`override_recording_filename_format` の挙動](#override_recording_filename_format-の挙動)
および
[`override_replay_buffer_filename_format` の挙動](#override_replay_buffer_filename_format-の挙動)
を参照）。

### バリデーションルール

| フィールド | ルール |
|------------|--------|
| `filter_uuid` | 36 文字の小文字正規 UUID（8-4-4-4-12 形式、小文字 16 進）— `obs_source_get_uuid()` の出力と一致 |
| `filter_uuid` | 既存の Branch Output フィルターソースに解決できること |
| `format` | 最大 1024 バイト |
| `format` | 相対パス表現のみ — 絶対パス（`/...`、`\...`、`X:\...`）、`..` セグメント、先頭の `~` は拒否 |

バリデーション失敗時は短い人間可読なエラー文字列を含めて
`{ "success": false, "error": "<message>" }` を返します。

### 呼び出し例

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

成功レスポンス:

```json
{ "success": true }
```

失敗レスポンス:

```json
{ "success": false, "error": "filter_uuid must be a lowercase canonical UUID string" }
```

## 既知の制限事項

以下の項目は実際の呼び出し側に影響しますが、API 契約の一部ではありません。本体のリファレンスを
読みやすく保つため、ここにまとめて記載します。

### OBS のシグナル / フロントエンドコールバックからは呼び出さないこと

`obs_source_signal`、`obs_output_signal`、`obs_frontend_event_callback` から proc handler の
プロシージャを呼び出す（または対応する vendor request を同コールバックから同期的に呼び出す）と
デッドロックする可能性があります。安全な呼び出し元: スクリプトタイマー、ホットキーハンドラ、
UI イベントハンドラ。ホットキーから呼び出す場合は、呼び出し時点で自分のコードが Branch
Output のロックを保持していないことを確認してください。

### 録画状態遷移中の遅延適用

録画が遷移中（保留中、分割中、リスタート中）の場合、オーバーライドは保存されますが、同期的に
適用されるのではなく最大 1 秒程度の遅延を伴って適用されます。proc / vendor 呼び出し自体は
すぐに復帰しますが、新しいフォーマットがアクティブになったことを通知する仕組みはありません。
遷移中にリクエストをバースト送信した場合、個々のリクエストが遷移完了までブロックすることも
あります。

### ライフタイム

`obs_module_unload()` 後にこれらのプロシージャや vendor request を呼び出してはいけません —
動作は未定義です。プロシージャと vendor request は `obs_module_post_load()` で登録されるため、
起動初期のウィンドウと競合する呼び出し側は、`out` パラメータを読む前に proc の戻り値を
チェックする必要があります。

### サーバ側スロットリングは未実装

プラグイン側では `override_recording_filename_format` /
`override_replay_buffer_filename_format` をレート制限していません。録画中に新しい `format`
が受理されるたびに、ファイル分割または録画リスタートのいずれかがトリガーされ、どちらにも
無視できない A/V コストがあります（後述）。**スロットリングはクライアント側で実装してください**
— 最低限、同一 `format` の重複を抑制し、異なる更新の間隔をレート制限すること（30 秒以上が
安全な目安）。

同梱の `recording-filename-from-text` スクリプトは内部でこのスロットリングを実装していますが、
vendor request はそのスクリプトを経由しません。同梱の `replay-buffer-filename-from-text`
スクリプトはリプレイバッファをリスタートしないため、スロットリングしていません。

### 頻繁なオーバーライド変更の A/V コスト

新しい `format` が受理されるたびに、以下の 2 経路のいずれかが走ります。

- **分割有効パス**（エンコーダ存続、ファイル境界のみ）:
  - 次のエンコーダキーフレームに揃えてカット。カットまでの遅延は設定されたキーフレーム間隔で
    上限が決まる（通常 ~2 秒。`keyint_sec=0` の場合は GOP 長がエンコーダバックエンドのネイティブ
    既定に従い、数秒に伸びる可能性あり）。
  - 新しいファイルごとにコンテナヘッダが書かれる。カット IDR 直前にあるリオーダされた B フレームは
    前のファイルへフラッシュされるためエンコード済みフレームの欠落は発生しないが、ファイル間の
    PTS 連続性は各ファイルのコンテナ `start_time` に依存する — B フレームを使うストリームを
    単純結合すると境界に小さなギャップが残る場合がある。
  - 新しいファイルの先頭に AAC エンコーダのプライミングサンプルが現れる（AAC-LC: 2112 サンプル
    = 2048 + 64、ISO/IEC 14496-3 準拠。HE-AAC ではさらに SBR 遅延 481 サンプルが加わる）。
    そのため単純結合するとカットごとに耳に聞こえるクリック音や短いギャップが発生し得る。
- **録画リスタートパス**（エンコーダ破棄＆再生成）:
  - ハードウェアエンコーダバックエンド（NVENC、QSV、AMF、VideoToolbox）はセッション / GPU
    コンテキストを再生成する。典型的なストール: 100 ms – 1 秒。コールドスタート時（プロセス
    起動後の最初のセッション、macOS VideoToolbox HEVC、特定の NVENC ドライバ状態など）は
    1 秒を超えることもある。
  - 遅延の大部分はセッション生成に起因し、キーフレーム間隔ではない。

これらのコストを十分に上回るスロットリング間隔をクライアント側で選択してください。

### `obs_data_get_string()` はキー欠落と空値を区別できない

libobs の規約上、`obs_data_get_string()` はキー欠落の場合も明示的な空文字列の場合も `""` を
返すため、プラグインからは両者を区別できません。

- `filter_uuid` の欠落は `""` として扱われ、`"filter_uuid is required"` で拒否される。
- `format` の欠落は `""` として扱われ、有効なオーバーライドを **クリアする**。

区別が重要な場合はキーを明示的に送信し、リクエスト形状をクライアント側でバリデートしてください。

### Lua サンプルスクリプト + Windows の非 ANSI パス

Lua バリアントの `recording-filename-from-text` / `replay-buffer-filename-from-text` が
モード「ファイルから読み込む」のテキストソースを読む際、ファイルは `io.open()` でオープン
されます。Windows では `io.open()` がパスをシステム ANSI コードページ（例: 日本語ロケールでは
CP932）として解釈するため、そのコードページ範囲外の文字を含むパスはオープンに失敗する場合が
あります。Python バリアントは CPython の Windows ワイド文字 API を利用しており影響を受けません。
Windows で UTF-8 パスのフルサポートが必要な場合は Python バリアントへ切り替えてください。
