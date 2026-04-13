# IDM2040-7A 操作パネルデモ — セットアップガイド

対象：**Windows + VS Code**、初心者向け段階解説  
ハードウェア：Bridgetek **IDM2040-7A**（RP2040 + BT817、800×480）  
ビルドツール：**pico-sdk + CMake + ARM GCC**

---

## 必要なものの全体像

| ツール | 役割 | 備考 |
|--------|------|------|
| pico-sdk Windows インストーラー | ARM GCC / CMake / Ninja / pico-sdk を一括導入 | 公式配布 |
| EveApps（Bridgetek） | BT817 を動かす EVE HAL ライブラリ | GitHub から clone |
| VS Code | エディタ + ビルド環境 | 拡張機能を追加 |
| Git | ライブラリの取得 | インストール済み ✅ |

---

## Step 1 — pico-sdk Windows インストーラーを導入する

これを入れると **ARM GCC / CMake / Ninja / pico-sdk / VS Code 拡張** がまとめて揃います。

1. ブラウザで以下を開く  
   `https://github.com/raspberrypi/pico-setup-windows/releases`

2. 最新版の **`pico-setup-windows-x64-standalone.exe`** をダウンロード

3. ダウンロードしたファイルをダブルクリックして実行
   - 「このアプリを許可しますか？」→ **はい**
   - インストール先はデフォルトのまま → **Install**
   - 完了まで数分かかります

4. インストール後、新しいコマンドプロンプト（または PowerShell）を開き確認

   ```
   cmake --version
   arm-none-eabi-gcc --version
   ```

   バージョン番号が表示されれば OK です。

---

## Step 2 — VS Code を開いて拡張機能を確認する

pico-sdk インストーラーが自動で VS Code に追加する拡張機能：

- **C/C++** (Microsoft)
- **CMake Tools** (Microsoft)
- **Raspberry Pi Pico** (raspberrypi)

### 確認手順

1. VS Code を開く
2. 左サイドバーの四角いアイコン（拡張機能）をクリック
3. 検索欄に `CMake Tools` と入力 → インストール済みなら **✓ インストール済み** と表示

もし入っていない場合は「インストール」ボタンをクリックしてください。

---

## Step 3 — EveApps（EVE ライブラリ）を取得する

BT817（EVE チップ）を動かすために Bridgetek 公式のライブラリが必要です。

1. **保存場所を決める**  
   例：`C:\EVE\EveApps` とします（別の場所でも構いません）

2. **Git Bash** または **コマンドプロンプト** を開き、以下を実行

   ```
   git clone https://github.com/Bridgetek/EveApps.git C:\EVE\EveApps
   ```

   ※ ダウンロードに数分かかります

3. 完了後、以下のフォルダが存在することを確認

   ```
   C:\EVE\EveApps\common\eve_hal\EVE_Hal.c
   C:\EVE\EveApps\common\eve_hal\EVE_HalImpl_RPIPICO.c
   ```

---

## Step 4 — 環境変数を設定する（ビルド時のパス解決）

CMake がライブラリの場所を知るために環境変数を設定します。

1. スタートメニューで「**環境変数**」と検索  
   →「**システム環境変数の編集**」をクリック

2. 「**環境変数(N)...**」ボタンをクリック

3. 「ユーザー環境変数」の「**新規(N)**」をクリック

4. 以下の2つを追加

   | 変数名 | 値（EveApps を置いた場所） |
   |--------|--------------------------|
   | `EVEAPPS_PATH` | `C:\EVE\EveApps` |
   | `PICO_SDK_PATH` | `C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\pico-sdk`（インストーラーが自動設定している場合は不要） |

   > `PICO_SDK_PATH` は既に設定されている場合があります。  
   > コマンドプロンプトで `echo %PICO_SDK_PATH%` を実行して確認してください。

5. **OK** → **OK** で閉じて、VS Code を**再起動**する

---

## Step 5 — プロジェクトを VS Code で開く

1. VS Code を起動
2. **ファイル → フォルダーを開く** で `C:\Users\kogane\panel-demo` を選択
3. 「このフォルダーの作成者を信頼しますか？」→ **はい、作成者を信頼します**

---

## Step 6 — ビルドする

1. VS Code 下部の青いバーに **「CMake: [Debug]」** が表示されていることを確認

2. **Ctrl+Shift+P** でコマンドパレットを開き  
   `CMake: Configure` と入力して実行

   > 初回は少し時間がかかります（数十秒）

3. キット（コンパイラ）の選択を求められたら  
   **「GCC for arm-none-eabi」** を選択

4. 設定が完了したら **Ctrl+Shift+P** → `CMake: Build` で実行

5. 下部のターミナルに `[100%] Built target panel_demo` と表示されれば成功

   ビルドファイルは `build/panel_demo.uf2` に生成されます。

---

## Step 7 — IDM2040-7A に書き込む

### UF2 ドラッグ＆ドロップ方式（最も簡単）

1. IDM2040-7A の **BOOTSEL ボタンを押しながら** USB ケーブルを PC に接続

2. PC に **RPI-RP2** というドライブが表示される

3. `build\panel_demo.uf2` をそのドライブにドラッグ＆ドロップ

4. 自動的に再起動し、パネルデモが起動します

---

## SPI 配線

| BT817 信号 | RP2040 GPIO |
|------------|-------------|
| SCK        | GP2         |
| MOSI       | GP3         |
| MISO       | GP4         |
| CS         | GP5         |
| PD (Reset) | GP6         |

※ IDM2040-7A はモジュール内部で接続済みのため、通常は配線不要です。

---

## トラブルシューティング

| 症状 | 原因 | 対処 |
|------|------|------|
| `cmake --version` が動かない | PATH が通っていない | PC 再起動後に再試行 |
| `Cannot find EVE_Hal.c` | EVEAPPS_PATH が未設定 | Step 4 を再確認、VS Code 再起動 |
| `PICO_SDK_PATH` が空 | インストーラーで設定されなかった | Step 4 で手動設定 |
| ビルドは通るが画面が映らない | SPI 配線ミス または タイミングパラメータ | `main.c` の GPIO 番号・Display タイミングを確認 |
| RPI-RP2 ドライブが出ない | BOOTSEL を押し忘れた | BOOTSEL を押しながら再接続 |

---

## ファイル構成

```
panel-demo/
├── main.c           # RP2040 エントリポイント・SPI/HAL 初期化
├── panel_demo.c     # 描画・タッチ・状態管理
├── panel_demo.h     # 定数・構造体・関数宣言
├── panel_sim.html   # PC ブラウザで動作確認できるシミュレータ
├── CMakeLists.txt   # ビルド定義（Step 5 で使用）
└── README.md        # このファイル
```

---

## 動作確認（実機不要）

実機がなくても `panel_sim.html` をブラウザで開くと  
PC 上で画面レイアウト・タッチ動作を確認できます。

1. `panel-demo` フォルダを開く
2. `panel_sim.html` をダブルクリック → ブラウザで起動
