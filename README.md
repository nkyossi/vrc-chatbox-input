# VRChat チャット入力

VRChat のデスクトップモードで、**どの IME でも**チャットボックスに日本語を入力するための小さな常駐アプリです。

VRChat は Easy Anti-Cheat で守られていて、Microsoft IME など一部の IME しか動きません（ATOK やひらがなIMEなどは止められます）。このアプリは VRChat の外の別のプロセスで入力を受けつけ、VRChat の公式の仕組み（OSC）でチャットボックスに送るので、どの IME でもつかえます。

## つかいかた

1. VRChat の設定で OSC を有効にします（アクションメニュー → Options → OSC → Enabled）。
2. `vrc-chatbox-input.exe` を起動します。通知領域にアイコンが出ます。いちど自動起動を設定しておけば（[自動で起動する](#自動で起動する)）、VRChat をはじめるときにはもう動いています。スタートメニューからも開けます（[スタートメニューから開く](#スタートメニューから開く)）。
3. VRChat で歩いたり見回したりしているときに **Y** キーをおすと、VRChat の画面の下のほうに入力欄が出ます。
4. 文字を入力して **Enter** をおすと、チャットボックスに送られて、VRChat の操作にもどります。**Esc** でとりやめます。

- 入力欄では、[ひらがなIME for Windows](https://github.com/nkyossi/hiragana-ime-for-windows) が入っていれば、ほかのアプリでえらんでいる IME（VRChat ではふつう Microsoft IME）にかかわらず、ひらがなIMEをつかいます（`--keep-ime` でそのまま）。
- 入力欄をひらくと、IME を自動でオン（あ）にします。
- 入力欄をひらいているあいだは、頭の上に「入力中」の表示が出ます。
- 送ったときは、このアプリが小さな音を鳴らします（VRChat の通知音も鳴ります）。
- チャットボックスの上限にあわせて、144 文字まで入力できます。
- メニューや検索欄をつかっているとき（マウスのカーソルが見えているとき）は、Y はふつうに VRChat にとどきます。
- Y をそのまま VRChat に渡したいときは、**Shift を押しながら Y**（VRChat 側には「Y」の文字が入ります）。しばらく VRChat の入力をつかいたいときは **Ctrl+Alt+Y** で Y キーの機能をオフにします。もう一度押すとオンにもどります（通知が出ます）。
- 通知領域のアイコンを右クリックすると、Y キーの機能のオン・オフと終了ができます（オン・オフは Ctrl+Alt+Y でも）。

## 自動で起動する

いちど設定しておけば、Windows にサインインしたときに自動で通知領域に常駐します。VRChat をはじめるときにはもう動いているので、そのまま Y キーがつかえます。

1. 通知領域のアイコンを右クリックします。
2. 「Windows と一緒に起動」をクリックして、チェックが付いていることをたしかめます。
3. 次に Windows にサインインしたときから自動で起動します。やめるときは、もう一度クリックしてチェックをはずします。

コマンドから設定することもできます（このときは設定だけをして終わります）。

```powershell
vrc-chatbox-input.exe --startup      # 自動起動にする
vrc-chatbox-input.exe --no-startup   # 自動起動をやめる
```

- 設定は `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` に、いまの exe の場所とオプションで登録されます。exe を別のフォルダに移したときは、新しい場所で一度起動すれば、登録も新しい場所に直ります（オプションはそのまま）。
- すでに動いているときに exe をもう一度開くと、通知領域に「すでに起動しています」と出ます。

## スタートメニューから開く

1. 通知領域のアイコンを右クリックします。
2. 「スタートメニューに登録」をクリックして、チェックが付いていることをたしかめます。
3. スタートメニューで「VRChat チャット入力」と入力すると見つかります。よく使うなら、右クリックして「スタートにピン留めする」もできます。やめるときは、もう一度クリックしてチェックをはずします。

コマンドから登録することもできます（このときは登録だけをして終わります）。

```powershell
vrc-chatbox-input.exe --start-menu      # 登録する
vrc-chatbox-input.exe --no-start-menu   # 登録を消す
```

## 軽さ

常駐していても、待っているあいだは CPU をつかいません（手元での実測: アイドル 15 秒で 0.0000 %、メモリ約 17 MB）。

- キーは低レベルフックで見はりますが、どのキーでも「Y かどうか」だけを見てすぐ返します。Y がおされたときだけ、前面の窓が VRChat かどうかを調べます。
- VRChat のプロセスを定期的に見にいくようなポーリングはしていません。
- 通知領域のメニューも、クリックされたときにだけ動きます。
- 入力欄をひらいているあいだだけ、IME をオンにするための短いタイマー（300 ms に一度）が動きます。

メモリは、入力欄のために RichEdit と IME（TSF）を読みこむぶんだけつかいます。

## オプション

```
vrc-chatbox-input.exe [--target VRChat.exe] [--port 9000] [--no-cursor-check] [--no-ime-open] [--keep-ime]
                      [--startup | --no-startup] [--start-menu | --no-start-menu]
```

| オプション | 意味 |
|:--|:--|
| `--target` | 対象のプログラム（既定: `VRChat.exe`） |
| `--port` | OSC の送り先のポート（既定: 9000） |
| `--no-cursor-check` | カーソルが見えていても Y で入力欄をひらく |
| `--no-ime-open` | 入力欄をひらいたときに IME をオンにしない |
| `--keep-ime` | ひらがなIMEに切りかえず、いまの IME をつかう |
| `--startup` | 自動起動の設定をして終わる |
| `--no-startup` | 自動起動の設定を消して終わる |
| `--start-menu` | スタートメニューに登録して終わる |
| `--no-start-menu` | スタートメニューの登録を消して終わる |

## ビルド

Visual Studio 2022（「C++ によるデスクトップ開発」）が必要です。

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Test
```

結果は `%LOCALAPPDATA%\vrc-chatbox-input-build` にできます。

実際のキー入力をためすテストは、VRChat のかわりの窓をひらいて SendInput で打ちます。実行中はキーボードとマウスにさわらないでください。

```powershell
powershell -ExecutionPolicy Bypass -File tools\run-e2e.ps1        # 窓のひらきかた・送信・Esc など
powershell -ExecutionPolicy Bypass -File tools\run-e2e.ps1 -Ime   # ひらがなIMEで、カーソルを動かしたあとも変換できるか
```

`-Ime` のときは、ひらがなIMEの設定を一時的にローマ字入力にして、おわったらもとにもどします。

## しくみ

- 低レベルのキーボードフック（`WH_KEYBOARD_LL`）で Y を見はります。VRChat のプロセスには何も入れません。
- 送信は UDP で `127.0.0.1:9000` に OSC の `/chatbox/input`（すぐに送る・通知音あり）と `/chatbox/typing` を送ります。
- 入力欄は TSF に対応した RichEdit なので、ひらがなIMEのモードレス入力もふつうのアプリと同じようにつかえます。

VRChat および Easy Anti-Cheat とは関係のない、非公式のツールです。
