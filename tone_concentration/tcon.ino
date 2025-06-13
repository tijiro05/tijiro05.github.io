#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// I2C型LCDデバイスのアドレスと行列幅を定義
constexpr unsigned char LCD_ADDR = 0x27; 
constexpr unsigned int  LCD_NUM_ROWS = 2;
constexpr unsigned int  LCD_NUM_COLS = 16;

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_NUM_ROWS, LCD_NUM_COLS);  // LCDのインスタンス化
/****************************************************************/

// I/OエキスパンダPCF8574のアドレスを定義
constexpr unsigned char PCF8574_ADDR_1 = 0x20;
constexpr unsigned char PCF8574_ADDR_2 = 0x24;
/**************************************************/

// Arduino使用ピンの設定
constexpr unsigned int SEED_PIN   = A0;  // 乱数生成用ピン
constexpr unsigned int BUZZER_PIN = 13;  // ブザー用ピン
constexpr unsigned int DECIDE_PIN = 12;  // 決定用ピン
constexpr unsigned int SER_PIN    = 6;   // シリアルデータ送信ピン
constexpr unsigned int LATCH_PIN  = 4;   // データラッチピン
constexpr unsigned int CLK_PIN    = 5;   // クロックピン
/****************************************/

// スイッチ行列の行数と列数を定義
constexpr unsigned int NUM_ROWS = 4;
constexpr unsigned int NUM_COLS = 4;
/***************************************/

// 音の周波数を定義（C5, D5, E5など、音楽の音階）
constexpr unsigned int C5  = 523;
constexpr unsigned int D5  = 587;
constexpr unsigned int E5  = 659;
constexpr unsigned int F5  = 698;
constexpr unsigned int G5  = 783;
constexpr unsigned int A5_ = 880;
constexpr unsigned int B5  = 987;
constexpr unsigned int C6  = 1046;
/***************************************/

// PCF8574から1バイトのデータを受け取る関数
unsigned char i2c_read_byte(int addr) {
  unsigned char data = 0x00;
  constexpr unsigned int NUM_BYTE = 1;
  
  Wire.requestFrom(addr, NUM_BYTE);  // I2C通信でデータを要求
  
  if (Wire.available())
    data = Wire.read();  // データを読み取る
  else
    lcd.print("Not connected");  // 接続が確認できなければエラーメッセージを表示
  
  return data;
}

// 効果音を管理する構造体
struct Sound_Effect {
  void beep(void) {
    noTone(BUZZER_PIN);
    tone(BUZZER_PIN, E5 / 4, 100);  // ピー音
    delay(150);
    noTone(BUZZER_PIN);
    tone(BUZZER_PIN, E5 / 4, 200);  // 短いブザー音
    delay(210);
    noTone(BUZZER_PIN);
  }

  void success(void) {
    noTone(BUZZER_PIN);
    tone(BUZZER_PIN, E5, 100);  // 成功音
    delay(100);
    noTone(BUZZER_PIN);
    tone(BUZZER_PIN, C5, 100);
    delay(100);
    noTone(BUZZER_PIN);
    tone(BUZZER_PIN, E5, 100);
    delay(100);
    noTone(BUZZER_PIN);
    tone(BUZZER_PIN, G5, 150);
    delay(150);
    noTone(BUZZER_PIN);
  }

  void complete(void) {
    // ゲーム終了時の音を後で追加予定
  }
};

// プレイヤーの情報を管理する構造体
struct Player {
  unsigned int score : 4;  // スコア（最大15）
  unsigned int selection_count : 5;  // 選択したスイッチ数（最大31）
  bool is_its_turn : 1;  // 自分のターンかどうか
  
  void set_selection_count(unsigned int val) {
    selection_count = val;
  }
  
  void set_score(unsigned int val) {
    score = val;
  }
  
  void set_turn_state(bool tof) {
    is_its_turn = tof;
  }
  
  void increment_score() {
    ++score;
  }
  
  void increment_selection_count(void) {
    ++selection_count;
  }
};

Player player1, player2;  // プレイヤー1とプレイヤー2のインスタンス

// スイッチの情報を管理する構造体
struct Switch {
  unsigned int row : 3;  // 行
  unsigned int col : 3;  // 列
  unsigned int tone_val;  // 音の周波数
  unsigned short flag = 0x0000;  // スイッチのフラグ
  
  //各座標とrow, colメンバーとの紐付けはgame構造体のshuffle()関数を参照
  bool is_pressed(void) {
    int num_sw = NUM_COLS * row + col;  // スイッチ番号を計算

    unsigned char data_lower_8bits = i2c_read_byte(PCF8574_ADDR_1);
    unsigned char data_upper_8bits = i2c_read_byte(PCF8574_ADDR_2);
    unsigned short data_combi_16bits = (data_upper_8bits << 8) | data_lower_8bits;  // 16ビットデータを組み合わせる

    return !(data_combi_16bits & (1 << num_sw));  // スイッチが押されているか確認
  }

  void play_tone(int period) {
    if (period)
      tone(BUZZER_PIN, tone_val, period);  // 音を鳴らす
    else
      noTone(BUZZER_PIN);  // 音を止める
  }
  
  bool is_revealed(){
    return flag > 0x0000;
  }

  void down_flag() {
    flag = 0x0000;  // スイッチの開閉状態を設定
  }
};

Switch SWs[NUM_ROWS][NUM_COLS];  // 4x4のスイッチ行列

// ゲームの情報を管理する構造体
struct Game {
  Sound_Effect se;  // 音を管理するインスタンス
  
  unsigned short flags_revealed;  // 現在表示されているフラグ
  
  void shuffle() {
    constexpr unsigned int NUM_TONES = 16;  // 音階数
    
    unsigned int tones[NUM_TONES] = {
      C5, C5, D5, D5, E5, E5, F5, F5,
      G5, G5, A5_, A5_, B5, B5, C6, C6
    };

    randomSeed(analogRead(SEED_PIN));  // 乱数シードを設定

    // スイッチ位置を設定
    for (int r = 0; r < NUM_ROWS; ++r) {
      for (int c = 0; c < NUM_COLS; ++c) {
        SWs[r][c].row = r;
        SWs[r][c].col = c;
      }
    }

    // 音階をシャッフル（乱数で並べ替える）
    for (int i = NUM_TONES - 1; i > 0; --i) {
      int j = random() % (i + 1);
      int temp = tones[i];
      tones[i] = tones[j];
      tones[j] = temp;
    }

    // スイッチに音を割り当て
    for (int r = 0; r < NUM_ROWS; ++r)
      for (int c = 0; c < NUM_COLS; ++c)
        SWs[r][c].tone_val = tones[NUM_COLS * r + c];
  }
  
  bool is_selection_comped(unsigned int slct_cnt){
    return slct_cnt >= 2;
  }
  
  bool is_match(unsigned int tone1, unsigned int tone2){
    return tone1 == tone2;
  }

  void display_players_info() {
    lcd.setCursor(0, 0);
    lcd.print("TURN  : ");
    lcd.print(player1.is_its_turn ? "PLAYER1" : "PLAYER2");

    lcd.setCursor(0, 1);
    lcd.print("SCORE : ");
    lcd.print(player1.is_its_turn ? player1.score : player2.score);
  }

  bool is_over(unsigned int score1, unsigned int score2) {
    return score1 + score2 >= NUM_ROWS * NUM_COLS / 2;  // ゲーム終了判定
  }
};

Game game;  // ゲームのインスタンス

// ゲームの状態を管理する列挙型
enum class GameState {
  Select,   // プレイヤーがスイッチを選んでいる状態
  Match,    // 2つのスイッチを比較している状態
  Result,   // 結果を表示している状態
  Judge,    // ゲーム終了判定中
  Confirm,  // 続行するかどうか選択中
  Reset,    // 新しいゲームの初期化
  End       // ゲーム終了・待機状態
};

GameState current_state;  // 現在のゲームの状態
bool will_continue;  // ゲームを続けるかどうか

// LEDの状態を2バイトで書き込む関数
void write_leds_2bytes(unsigned short data_2bytes) {
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(SER_PIN, CLK_PIN, MSBFIRST, data_2bytes >> 8);  // 高位8ビットを送信
  shiftOut(SER_PIN, CLK_PIN, MSBFIRST, data_2bytes & 0xFF);  // 低位8ビットを送信
  digitalWrite(LATCH_PIN, HIGH);  // ラッチピンをHIGHにしてデータを反映
}

// 初期化関数
void init_pins(void) {
  pinMode(BUZZER_PIN, OUTPUT);  // ブザーのピンを出力に設定
  pinMode(SER_PIN, OUTPUT);  // シリアルデータ送信ピンを出力に設定
  pinMode(LATCH_PIN, OUTPUT);  // データラッチピンを出力に設定
  pinMode(CLK_PIN, OUTPUT);  // クロックピンを出力に設定
  pinMode(DECIDE_PIN, INPUT_PULLUP);  // 決定用ピンをプルアップ入力に設定
}

// LCD初期化関数
void init_lcd(void) {
  lcd.init();  // LCDを初期化
  lcd.clear();  // LCDをクリア
  lcd.backlight();  // LCDのバックライトをオン
}

void setup() {
  Wire.begin();  // I2C通信開始
  init_pins();  // ピンの初期化
  init_lcd();  // LCDの初期化

  write_leds_2bytes(0xFFFF);  // 全てのLEDを点灯
  lcd.setCursor(5, 0);
  lcd.print("HELLO!");  // LCDに挨拶を表示

  delay(2000);

  write_leds_2bytes(0x0000);  // LEDを消灯
  lcd.clear();  // LCDをクリア

  player1.set_score(0);  // プレイヤー1のスコア初期化
  player1.set_selection_count(0);  // プレイヤー1の選択回数初期化
  player1.set_turn_state(true);  // プレイヤー1がターン

  player2.set_score(0);  // プレイヤー2のスコア初期化
  player2.set_selection_count(0);  // プレイヤー2の選択回数初期化
  player2.set_turn_state(false);  // プレイヤー2はターンではない
  
  game.flags_revealed = 0x0000;  // フラグ初期化
  game.shuffle();  // スイッチの音階をシャッフル
  game.display_players_info();  // プレイヤーの情報を表示

  current_state = GameState::Select;  // ゲームの初期状態を選択に設定
}

constexpr unsigned int NUM_PRESSED_SWs = 2;
Switch *pressed_sw_infos[NUM_PRESSED_SWs];  // 選択されたスイッチ情報

void loop() {
  Player &curr_player = (player1.is_its_turn) ? player1 : player2;
  Player &next_player = (player1.is_its_turn) ? player2 : player1;

  switch (current_state) {
    case GameState::Select: {
      // プレイヤーがスイッチを選んでいる状態
      for (int r = 0; r < NUM_ROWS; ++r) {
        for (int c = 0; c < NUM_COLS; ++c) {
          if (SWs[r][c].is_pressed() && !SWs[r][c].is_revealed()) {
            constexpr unsigned int TONE_PERIOD_ms = 330;
            SWs[r][c].play_tone(TONE_PERIOD_ms);
            SWs[r][c].flag = 1 << (NUM_COLS * r + c);
            game.flags_revealed |= SWs[r][c].flag;
            write_leds_2bytes(game.flags_revealed);

            pressed_sw_infos[curr_player.selection_count] = &SWs[r][c];
            curr_player.increment_selection_count();

            // 2つ選んだら次のステップへ
            if (game.is_selection_comped(curr_player.selection_count))
              current_state = GameState::Match;

            return;
          }
        }
      }
      delay(300);  // デバウンス処理
      break;
    }

    case GameState::Match: {
      // 2つのスイッチを比較している状態
      delay(600);
      if (game.is_match(pressed_sw_infos[0]->tone_val, pressed_sw_infos[1]->tone_val)) {
        curr_player.increment_score();
        game.se.success();
      } else {
        game.flags_revealed &= ~(pressed_sw_infos[0]->flag | pressed_sw_infos[1]->flag);
        write_leds_2bytes(game.flags_revealed);

        pressed_sw_infos[0]->down_flag();
        pressed_sw_infos[1]->down_flag();

        curr_player.set_turn_state(false);
        next_player.set_turn_state(true);

        game.se.beep();
      }

      curr_player.set_selection_count(0);
      current_state = GameState::Result;
      break;
    }

    case GameState::Result: {
      // 結果を表示している状態
      delay(200);
      game.display_players_info();

      if (game.is_over(player1.score, player2.score))
        current_state = GameState::Judge;
      else
        current_state = GameState::Select;

      break;
    }

    case GameState::Judge: {
      // ゲーム終了判定中
      delay(1000);
      lcd.clear();
      lcd.setCursor(2, 0);
      lcd.print("WINNER IS...");
      lcd.setCursor(2, 1);

      if (player1.score > player2.score)
        lcd.print("  PLAYER1!");
      else if (player2.score > player1.score)
        lcd.print("  PLAYER2!");
      else
        lcd.print(" PLAYER1&2!");

      delay(3000);
      lcd.clear();
      lcd.setCursor(3, 0);
      lcd.print("CONTINUE?");
      lcd.setCursor(3, 1);
      lcd.print(">YES  NO");

      will_continue = true;
      current_state = GameState::Confirm;
      break;
    }

    case GameState::Confirm: {
      // 続行するかどうか選択中
      while (digitalRead(DECIDE_PIN) == HIGH) {
        for (int r = 0; r < NUM_ROWS; ++r) {
          for (int c = 0; c < NUM_COLS; ++c) {
            if (SWs[r][c].is_pressed()) {
              will_continue = !will_continue;
              lcd.setCursor(3, 1);
              lcd.print(will_continue ? ">YES  NO" : " YES >NO");
              delay(100);
              break;
            }
          }
        }
      }

      current_state = will_continue ? GameState::Reset : GameState::End;
      break;
    }

    case GameState::Reset: {
      // 新しいゲームの初期化
      lcd.clear();
      lcd.setCursor(1, 0);
      lcd.print("LOADING");
      delay(500);
      lcd.print("LOADING.");
      delay(500);
      lcd.print("LOADING..");
      delay(500);
      lcd.print("LOADING...");
      delay(500);

      player1.set_score(0);
      player1.set_selection_count(0);
      player1.set_turn_state(true);

      player2.set_score(0);
      player2.set_selection_count(0);
      player2.set_turn_state(false);

      game.flags_revealed = 0x0000;
      write_leds_2bytes(game.flags_revealed);

      for (int r = 0; r < NUM_ROWS; ++r)
        for (int c = 0; c < NUM_COLS; ++c)
          SWs[r][c].down_flag();

      game.shuffle();
      game.display_players_info();

      current_state = GameState::Select;
      break;
    }

    case GameState::End: {
      // ゲーム終了・待機状態
      lcd.clear();
      lcd.setCursor(1, 0);
      lcd.print("THANK YOU");
      lcd.setCursor(4, 1);
      lcd.print("FOR PLAYING!");
      while (true) {}
      break;
    }
  }
}
