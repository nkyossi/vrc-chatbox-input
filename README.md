# VRChat チャット入力

VRChat のデスクトップモードで、**どの IME でも**チャットボックスに日本語を入力するための小さな常駐アプリです。

VRChat は Easy Anti-Cheat で守られていて、Microsoft IME など一部の IME しか動きません（ATOK やひらがなIMEなどは止められます）。このアプリは VRChat の外の別のプロセスで入力を受けつけ、VRChat の公式の仕組み（OSC）でチャットボックスに送るので、どの IME でもつかえます。

## つかいかた

1. VRChat の設定で OSC を有効にします（アクションメニュー → Options → OSC → Enabled）。
2. `vrc-chatbox-input.exe` を起動します。通知領域にアイコンが出ます。
3. VRChat で歩いたり見回したりしているときに **Y** キーをおすと、VRChat の画面の下のほうに入力欄が出ます。
4. 文字を入力して **Enter** をおすと、チャットボックスに送られて、VRChat の操作にもどります。**Esc** でとりやめます。

- 入力欄をひらくと、IME を自動でオン（あ）にします。
- 入力欄をひらいているあいだは、頭の上に「入力中」の表示が出ます。
- 送ったときは、このアプリが小さな音を鳴らします（VRChat の通知音も鳴ります）。
- チャットボックスの上限にあわせて、144 文字まで入力できます。
- メニューや検索欄をつかっているとき（マウスのカーソルが見えているとき）は、Y はふつうに VRChat にとどきます。
- 通知領域のアイコンを右クリックすると、Y キーの機能のオン・オフと終了ができます。

## オプション

```
vrc-chatbox-input.exe [--target VRChat.exe] [--port 9000] [--no-cursor-check] [--no-ime-open]
```

| オプション | 意味 |
|:--|:--|
| `--target` | 対象のプログラム（既定: `VRChat.exe`） |
| `--port` | OSC の送り先のポート（既定: 9000） |
| `--no-cursor-check` | カーソルが見えていても Y で入力欄をひらく |
| `--no-ime-open` | 入力欄をひらいたときに IME をオンにしない |

## ビルド

Visual Studio 2022（「C++ によるデスクトップ開発」）が必要です。

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Test
```

結果は `%LOCALAPPDATA%\vrc-chatbox-input-build` にできます。

実際のキー入力をためすテストは、VRChat のかわりの窓をひらいて SendInput で打ちます。実行中はキーボードとマウスにさわらないでください。

```powershell
$b = "$env:LOCALAPPDATA\vrc-chatbox-input-build"
& "$b\e2e_test.exe" "$b\vrc-chatbox-input.exe"
```

## しくみ

- 低レベルのキーボードフック（`WH_KEYBOARD_LL`）で Y を見はります。VRChat のプロセスには何も入れません。
- 送信は UDP で `127.0.0.1:9000` に OSC の `/chatbox/input`（すぐに送る・通知音あり）と `/chatbox/typing` を送ります。
- 入力欄は TSF に対応した RichEdit なので、ひらがなIMEのモードレス入力もふつうのアプリと同じようにつかえます。

VRChat および Easy Anti-Cheat とは関係のない、非公式のツールです。
