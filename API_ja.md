# Branch Output API リファレンス

[English version](./API.md)

## レコーディングファイル名オーバーライド

### 概要

レコーディングファイル名オーバーライドは、Branch Output の公開されたプロシージャを使ってストリーム録画およびリプレイバッファ保存のファイル名フォーマットをオーバーライドすることができる機能です。

オーバーライドしたファイル名フォーマットは、フィルタープロパティで設定したものとは別に保持され、プロシージャでリセットするか OBS を終了するまで有効化されます。

この機能は、たとえば現在のシーンやテキストインプットの値、その他の外部データによってファイル名をオーバーライドすることで、録画ファイルを整理した状態で保存したいというプロダクションの要請で実装されました。

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

サンプルスクリプトは Python で書かれていますので、使用する前に OBS メニューの Tools → Scripts で Python Settings が正しく設定されているか確認してください。

#### recording-filename-from-text.py

テキストインプットの値を読み取って、ストリーム録画ファイル名フォーマットに反映するスクリプトです。

1. OBS のメニューから Tools → Scripts を開く
2. Scripts ダイアログ下部のプラスボタンをクリック
3. スクリプトファイルを選択。
   通常、OBSインストールパスの `data\obs-plugins\osi-branch-output/scripts/recording-filename-from-text.py` にインストールされています（Windowsの場合）
4. Loaded Scripts でスクリプトを選択すると、Description で各種設定が行えます。
   - **Text Source** - テキストインプットを選択
   - **Branch Output Filter** - オーバーライドする Branch Output フィルターを選択
   - **Base Filename Format** - ベースとなるファイル名フォーマットを指定。これらのフォーマットはファイル名の末尾に付与されます。
5. 設定を行った時点でオーバーライドが有効です。Script Log をクリックするとスクリプトの動作状況を確認できます。
   例： `[recording-filename-from-text.py] Recording filename format updated: test %CCYY-%MM-%DD %hh-%mm-%ss`
6. オーバーライド有効の状態で録画するとファイル名はオーバーライドされたものが優先使用されます。
7. オーバーライドを無効化したい場合はスクリプトをゴミ箱ボタンで Loaded Scripts から削除してください。

> **録画中の挙動**
>
> - 録画開始前: 録画開始時にオーバーライドされたファイル名フォーマットが使用されます
> - 録画中（ファイル分割有効時）: ファイル名フォーマットが変化した場合、ファイルスプリットが即座に実行されます
> - 録画中（ファイル分割無効時）: 新しいファイル名フォーマットで、録画がリスタートされます

**注意:** オーバーライドが有効な状態で、フィルタープロパティ設定のファイル名は使用されません。

#### replay-buffer-filename-from-text.py

テキストインプットの値を読み取って、リプレイバッファー保存ファイル名フォーマットに反映するスクリプトです。

1. OBS のメニューから Tools → Scripts を開く
2. Scripts ダイアログ下部のプラスボタンをクリック
3. スクリプトファイルを選択。
   通常、OBSインストールパスの `data\obs-plugins\osi-branch-output/scripts/replay-buffer-filename-from-text.py` にインストールされています（Windowsの場合）
4. Loaded Scripts でスクリプトを選択すると、Description で各種設定が行えます。
   - **Text Source** - テキストインプットを選択
   - **Branch Output Filter** - オーバーライドする Branch Output フィルターを選択
   - **Base Filename Format** - ベースとなるファイル名フォーマットを指定。これらのフォーマットはファイル名の末尾に付与されます。
5. 設定を行った時点でオーバーライドが有効です。Script Log をクリックするとスクリプトの動作状況を確認できます。
   例： `[replay-buffer-filename-from-text.py] Replay buffer filename format updated: test %CCYY-%MM-%DD %hh-%mm-%ss`
6. オーバーライド有効の状態で保存するとファイル名はオーバーライドされたものが優先使用されます。
7. オーバーライドを無効化したい場合はスクリプトをゴミ箱ボタンで Loaded Scripts から削除してください。

**注意:** オーバーライドが有効な状態で、フィルタープロパティ設定のファイル名は使用されません。
