# Branch Output filter (The OBS Studio Plugin)

> ## スポンサープログラム
> 
> プロジェクトにご協力いただける方は、是非ご支援ください。
> 
> [<img src="https://img.shields.io/static/v1?label=Sponsor&message=%E2%9D%A4&logo=GitHub&color=%23fe8e86" />](https://github.com/sponsors/OPENSPHERE-Inc)


[<img src="./screenshot1.jpg" />](./screenshot1.jpg)

[<img src="./screenshot2.jpg" />](./screenshot2.jpg)

## Features

この OBS Studio プラグインでは、ソース毎に配信ないし録画するエフェクトフィルタを追加します。
[Source Record](https://github.com/exeldro/obs-source-record) プラグインに触発されて開発しましたが、ストリーミングでの使用に重点が置かれています。
より信頼性があり、適切なオーディオの取り扱いを行います。

- ソースまたはシーンのエフェクトフィルタに「Branch Output」を追加
- フィルター1つにつき1本のストリーム送出が、専用のエンコーディング設定で可能
- 1つのソース・シーンに複数の Branch Output を追加可能（PCのスペックが許す限り追加可能）
- Branch Output フィルターごとに音声ソースを選択可能（フィルター音声、任意ソース音声、音声トラック1～6）
- 接続が切れても自動的に再接続
- 配信録画機能（各種コンテナ形式、時間・サイズ分割に対応）
  
  ※接続情報を空欄にすれば録画のみとして動作

- ステータスドックで全 Branch Output フィルターの状態と統計を確認可能。一括ないし個別の有効化・無効化に対応。
- OBS Studio の配信・録画・仮想カメラの状態と連動可能

> **スタジオモード向け: Branch Output はスタジオモードのプログラム出力を無視し、常にプレビューを出力に使用します**

## Requirements

[OBS Studio](https://obsproject.com/) >= 30.1.0 (Qt6, x64/ARM64/AppleSilicon)

# Installation

Please download latest install package from [Release](https://github.com/OPENSPHERE-Inc/branch-output/releases)

# User manual

[こちらのブログ記事](https://blog.opensphere.co.jp/posts/branchoutput001) に日本語でより詳しい使い方を掲載していますので参照ください。

1. 任意の「ソース」または「シーン」に、エフェクトフィルタとして "Branch Output" を追加
   （注意：「シーン」はデフォルトでオーディオがありません）
2. サーバーURLとストリームキーを入力。
   サーバーURLは OBS のカスタム配信設定の様に RTMP や SRT 等を使用できます。
3. オーディオソースを選択。
   カスタムオーディオソースからチェックを外した場合、フィルターオーディオを使用します
   （注意：「シーン」の音声は必ずカスタムオーディオソースを使用しなければなりません）

   「任意のソース」はフィルターパイプラインの後、オーディオミキサーの前で取り込まれます。
   「音声トラック1～6」はオーディオミキサーの出力が取り込まれます。

   「無音」も選択可能です。
4. 音声および映像エンコーダーを設定。NVENC 等のハードウェアエンコーダーも使用可能です。
5. 「適用」ボタンをクリックすると、送信が開始されます。
6. 「目」アイコンでフィルターが非アクティブ化されると、出力ストリームもオフラインになります。

※いくつかのソース（例：ローカルメディアソース）は、シーンが非アクティブの場合に送出が停止しますが、これはプラグインのバグではありません。

# TIPS

## 1. 解像度を変更しつつレイアウト変更して配信したい

> 解像度変更機能は 1.0.0 で統合されています。
> 
> 以下の手順は、より柔軟性のある別の実現方法です(クロッピング、拡大、黒背景を追加等)

1. キャンバス解像度は 1080p であること前提とします。
2. 新たにブランクシーンを作成
3. 配信したいソースを右クリックしてメニューから「コピー」を実行
4. 1で作成したブランクシーンで右クリックしてメニューから「貼り付け（参照）」を実行。
   必要に応じて拡大縮小、クロッピングしてください。
5. シーンのエフェクトフィルタに Branch Output を追加して配信設定

貼り付け（参照）の場合はソースを複製しないので、デバイス競合を起こすことはありません。

Branch Output はシーンがアクティブでなくとも配信を行います。
この方法はシーンがアクティブでないと再生されない一部のソース（例えばメディアソース）を除いてうまく動作するかと思います。

## 2. プログラムアウトを複数の配信プラットフォームに配信したい

> **重要**: 映像のループを防ぐために、スタジオモードで作業してください。

[Main View Source](https://obsproject.com/forum/resources/main-view-source.1501/) というプラグインを併用すると可能です。

1. 新たにブランクシーンを作成（配信用シーン）
2. 配信用シーンに Main View Source を追加する
4. シーンのエフェクトフィルタに Branch Output を追加して配信設定

また、複数の配信用シーンを作成して、追加のソースを重ねれば、配信プラットフォームごとに内容を変更することができます。
例えば Twitch には Twitch のコメント、YouTube には YouTube のコメントを表示したい、という要件が該当します。

ただし、この方法ではソースの追加はできますが、削除はできません

# Development

This plugin is developed under [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)

