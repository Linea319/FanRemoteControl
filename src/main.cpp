#include <M5StickCPlus.h>
#include <IRrecv.h>
#include <IRsend.h>
#include "commnads.h"
#include "micFunc.h"

//IRUNITを接続しない場合は消す
//#define IR_UNIT

enum RemoconState:int{
  Recieve,
  Setting,
  TimerMode,
  SendTest,
  BatteryCheck,
  MicCheck,
  Max
};

#ifndef IR_UNIT
const uint8_t kIRSendPin = 9;
#else
const uint8_t kIRRecievePin = 33;
const uint8_t kIRSendPin = 32;
#endif

const std::string stateStr[] = {"IR Recieve","Timer Set","Enable Timer","Send  Test","Battery","Mic Test"};

#ifdef IR_UNIT
IRrecv irReciever(kIRRecievePin);
#endif
IRsend irSender(kIRSendPin);

uint16_t timerHour = 1;
RemoconState state = RemoconState::Recieve;
std::vector<uint64_t> commands;
xTaskHandle hTimerTask = nullptr;

void recieveState()
{
#ifndef IR_UNIT
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0, 1);
  M5.Lcd.println("current commands");
  char txt[64];
  for (int i=0;i<commands.size();i++)
  {
    sprintf(txt, " %d: %" PRIu64 ,i,commands[i]);
    M5.Lcd.println(txt);
  }
  delay(250);
#else
  //Aボタン押したらコマンドリセット
  if(M5.BtnA.wasPressed()){
    commands.clear();
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0, 1);
    M5.Lcd.println("commands clear!");
  }

  // 受信
  decode_results results;
  if (irReciever.decode(&results)) {
    // 受信した
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0, 1);
    M5.Lcd.println("IR recieved!");
    char txt[64];
    sprintf(txt,"type:%d",results.decode_type);
    M5.Lcd.println(txt);
    sprintf(txt,"address:%d",results.address);
    M5.Lcd.println(txt);
    sprintf(txt,"command:%d",results.command);
    M5.Lcd.println(txt);
    sprintf(txt, "value:%" PRIu64,results.value);
    M5.Lcd.println(txt);

    //コマンド登録
    commands.push_back(results.value);
    M5.Lcd.println("");
    M5.Lcd.println("current commands");
    for (int i=0;i<commands.size();i++)
    {
      sprintf(txt, " %d: %" PRIu64 ,i,commands[i]);
      M5.Lcd.println(txt);
    }

    irReciever.resume();
  }
#endif
}

void settingState()
{
  //A押したらタイマー増やす
  if(M5.BtnA.isPressed())
  {
    timerHour++;
    //とりあえず上限8時間
    if(timerHour > 8)
    {
      timerHour = 1;
    }

    char txt[64];
    sprintf(txt,"Send %d Hour After",timerHour);
    M5.Lcd.drawCentreString(txt,M5.Lcd.width()/2,M5.Lcd.height()/2,1);

    delay(500);
  }
}

void send()
{
    // 画面初期化
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0, 1);
    M5.Lcd.println("IR Send!");
    M5.Lcd.println("");
    char txt[64];

  //送信
    int16_t commandCount = commands.size();
    if (commandCount > 0) {
      for (int16_t i = 0; i < commandCount; i++)
      {
        sprintf(txt, " %d: %" PRIu64,i,commands[i]);
        M5.Lcd.println(txt);
        irSender.sendSymphony(commands[i]);

        delay(1500);
      }

  }
}

void drawState()
{
    M5.lcd.fillScreen(BLACK);
    char txt[64];
    sprintf(txt,"state %s",stateStr[state].c_str());
    M5.Lcd.drawCentreString(txt,M5.Lcd.width()/2,M5.Lcd.height()/2,1);
}

void setup() {
  M5.begin();
  setCpuFrequencyMhz(80);//80Mhzより低いとIRがうまく取得できなかった

  M5.Axp.ScreenBreath(9);
  M5.Lcd.setRotation(1);
  M5.Lcd.setTextFont(1);

  // GPIO37(M5StickCのHOMEボタン)かGPIO39(M5StickCの右ボタン)がLOWになったら起動
  pinMode(GPIO_NUM_37, INPUT_PULLUP);
  gpio_wakeup_enable(GPIO_NUM_37, GPIO_INTR_LOW_LEVEL);

  //マイク初期化
  i2sInit();

  //デフォルトは電源ON,首振り
  commands = {kCommandPower,kCommandSwing};

  delay(500);
#ifdef IR_UNIT
  irReciever.enableIRIn();
#endif
  irSender.begin();

  delay(500);

  drawState();
}

void loop() {

  //btn更新
  M5.update();
  if(M5.BtnB.isPressed())
  {
    int stateVal = (int)state+1;
    if(stateVal >= RemoconState::Max)
      stateVal = RemoconState::Recieve;
    state = (RemoconState)stateVal;

    drawState();

    //マイクタスクの切り替え
    if(state == RemoconState::MicCheck){
      xTaskCreate(mic_record_task, "mic_record_task", 2048, NULL, 1, &hTimerTask);
    }
    else
    {
      if(hTimerTask != nullptr)
      {
        vTaskDelete(hTimerTask);
        hTimerTask = nullptr;
      }
    }

    //1秒待つ
    delay(1000);
  }

  switch (state)
  {
  case RemoconState::Recieve:
    recieveState();
    break;
  case RemoconState::Setting:
    settingState();
    break;
  case RemoconState::TimerMode:
    if(M5.BtnA.wasPressed())
    {
      //時間表示
      char txt[64];
      sprintf(txt,"Send %d Hour After",timerHour);
      M5.Lcd.drawCentreString(txt,M5.Lcd.width()/2,M5.Lcd.height()/2,1);
      delay(3000);

      //スリープセット
      M5.Axp.ScreenBreath(0);
      Serial.printf("enter sleep:%ld\n",SLEEP_SEC(timerHour));

      esp_sleep_enable_gpio_wakeup();
      esp_sleep_enable_timer_wakeup(SLEEP_HR(timerHour));
      //esp_sleep_enable_timer_wakeup(SLEEP_SEC(timerHour)); //秒数テスト
      esp_light_sleep_start();

      //起きたら送信
      Serial.println("wakeup sleep");
      M5.Axp.begin();
      M5.Axp.ScreenBreath(8);
      send();
      delay(2000);

      //電源切る
      M5.Axp.PowerOff();
    }
    break;
  case RemoconState::SendTest:
    if(M5.BtnA.wasPressed())
    {
      //送信
      send();

      delay(500);
    }
    break;
  case RemoconState::BatteryCheck:
    char txt[64];
    sprintf(txt,"Battery Voltage: %.2f",M5.Axp.GetBatVoltage());
    M5.Lcd.drawCentreString(txt,M5.Lcd.width()/2,M5.Lcd.height()/2,1);
    delay(250);
    break;
  default:
    delay(100);
    break;
  }

  delay(1);
}