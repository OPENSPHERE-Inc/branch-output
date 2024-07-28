# Source Output filter (The OBS Studio Plugin)

## Features

**[EN]**

Add filters to be sent out in RTMP / SRT etc. for each source.
Inspired by the [Source Record](https://github.com/exeldro/obs-source-record) plugin, but more focused on streaming.
More reliable and proper audio handling.

(*) Recording functionality is currently not available

- Added “Source Output” to source or scene effect filters.
- One stream per Source Output filter can be sent with dedicated encoding settings.
- Multiple Source Outputs can be added to a single source or scene (as PC specs allow)
- Source Output Selectable audio source for each filter (filter audio, any source audio, audio tracks 1-6)
- Automatically reconnects when disconnected


**[JP]**

ソース毎に RTMP / SRT 等で送出するフィルターを追加します。
[Source Record](https://github.com/exeldro/obs-source-record) プラグインに触発されて開発しましたが、ストリーミングでの使用に重点が置かれています。
より信頼性があり、適切なオーディオの取り扱いを行います。

※録画機能は現在のところ非搭載

- ソースまたはシーンのエフェクトフィルターに「Source Output」を追加
- Source Output フィルター1つにつき1本のストリーム送出が、専用のエンコーディング設定で可能  
- 1つのソース・シーンに複数の Source Output を追加可能（PCのスペックが許す限り追加可能）
- Source Output フィルターごとに音声ソースを選択可能（フィルター音声、任意ソース音声、音声トラック1～6）
- 接続が切れても自動的に再接続

## Requirements

[OBS Studio](https://obsproject.com/) >= 30.0.0 (Qt6, x64/ARM64/AppleSilicon)

# Installation

Please download latest install package from [Release]()

# User manual

**[EN]**

1. Add "Source Output" as effect filters to any "Source" or "Scene" (NOTE: "Scene" has no audio defaultly)
2. Input server URL and stream key. The server URL can be RTMP or SRT etc. like OBS's custom stream settings.
3. Choose audio source. Un-checked custom audio source means use filter audio as source (NOTE: "Scene"
   must has custom audio source for it's sound)

   "Any Sources" will be captured after filter pipeline before Audio Mixer. Also "Audio track 1 ~ 6" will be captured from Audio Mixer output.  
   
   You can choose "No Audio" as well.  
   
4. Setup encoder. It's usable that hardware encoder such as NVENC.
5. Press Apply button and stream will be online.

(*) Some sources (e.g. Local Media source) will stop stream output during inactivated scene. It's not plugin's bug.

**[JP]**

日本語の詳細マニュアルが [こちらのブログ記事]() にあります。

1. 任意の「ソース」または「シーン」に、エフェクトフィルターとして "Source Output" を追加
   （注意：「シーン」はデフォルトでオーディオがありません）
2. サーバーURLとストリームキーを入力。
   サーバーURLは OBS のカスタム配信設定の様に RTMP や SRT 等を使用できます。
3. オーディオソースを選択。
   カスタムオーディオソースからチェックを外した場合、フィルターオーディオを使用します
   （注意：「シーン」の音声は必ずカスタムオーディオソースを使用しなければなりません）

   「任意のソース」はフィルターパイプラインの後、オーディオミキサーの前で取り込まれます。
   「音声トラック1～6」はオーディオミキサーの出力が取り込まれます。

   「無音」も選択可能です。
4. エンコーダーを設定。NVENC 等のハードウェアエンコーダーも使用可能です。
5. 「適用」ボタンをクリックすると、送信が開始されます。

※いくつかのソース（例：ローカルメディアソース）は、シーンが非アクティブの場合に送出が停止しますが、これはプラグインのバグではありません。

# Development

This plugin is developed under [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)

