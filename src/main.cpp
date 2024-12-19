#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>

#include "HidLampArray.h"
#include "LampArrayReports.h"

uint8_t const desc_lighting_report[] = {
    TUD_HID_REPORT_DESC_LIGHTING(0x01)
};

// 信号を送るGPIOピン
#define FAN_LED_PIN 10

// ファンとLEDの総数
#define FAN_COUNT 3
#define FAN_LED_COUNT 8
#define TOTAL_LED_COUNT (FAN_LED_COUNT * FAN_COUNT)
#define NEO_PIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// デバイスの物理的な大きさ
#define FAN_WIDTH_MM 120
#define FAN_TOTAL_WIDTH_MM (FAN_WIDTH_MM * FAN_COUNT)
#define FAN_HEIGHT_MM 120
#define FAN_DEPTH_MM 25
#define FAN_LED_RADIUS 17
#define FAN_LED_DEPTH 5

// ランプが遅延するミリ秒
#define LAMP_UPDATE_LATENCY (0x04)

// AR12 Proの場合は、排気側を床面に向けたとき
// 左下から時計回りにLEDが配置されている
const int FAN_LED_POS_MM[][2] = {
    {-11, 28},
    {-28, 11},
    {-28,-11},
    {-11,-28},
    { 11,-28},
    { 28,-11},
    { 28, 11},
    { 11, 28},
};


Adafruit_USBD_HID usb_hid;
Adafruit_NeoPixel leds = Adafruit_NeoPixel(TOTAL_LED_COUNT, FAN_LED_PIN, NEO_PIXEL_TYPE);
uint32_t backup_colors[TOTAL_LED_COUNT];

uint16_t last_lamp_id;
bool is_autonomous, color_reset;
bool is_completed, require_update;
uint8_t usb_suspend_mode = 0;

uint16_t OnGetReport(uint8_t report_id, hid_report_type_t report_type, uint8_t* data, uint16_t length);
void OnSetReport(uint8_t report_id, hid_report_type_t report_type, uint8_t const* data, uint16_t length);

void setup()
{
    TinyUSBDevice.setManufacturerDescriptor("TNK Software");
    TinyUSBDevice.setProductDescriptor("Pico Fan Controller");

    if (!TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.begin(0);
    }

    usb_hid.enableOutEndpoint(true);
    usb_hid.setPollInterval(2);
    usb_hid.setReportDescriptor(desc_lighting_report, sizeof(desc_lighting_report));
    usb_hid.setReportCallback(OnGetReport, OnSetReport);
    usb_hid.begin();

    //Serial.begin(115200);

    leds.begin();
    leds.clear();
	leds.show();

    last_lamp_id = 0;
    is_autonomous = true;
    color_reset = false;
    is_completed = false;
    require_update = false;
    
    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(10);
        TinyUSBDevice.attach();
    }
}

void loop() 
{
#ifdef TINYUSB_NEED_POLLING_TASK
    TinyUSBDevice.task();
#endif

    if(is_completed == true && is_autonomous == false){
        // シリアル通信をしている状態だと即座に反応しないことがある点に注意
        if(require_update == true){
            leds.show();
            color_reset = false;
        }
        require_update = false;
        is_completed = false;
    }else if(is_autonomous == true && color_reset == false){
        leds.clear();
        leds.show();
        color_reset = true;
    }
}

// USBがサスペンド状態になったときにTinyUSBから呼び出される
void tud_suspend_cb(bool remote_wakeup_en)
{
    // 直近のデータを保存してから消灯
    for (int i = 0; i < TOTAL_LED_COUNT; i++) backup_colors[i] = leds.getPixelColor(i);
    leds.clear();
    leds.show();

    __wfi();    // Pythonのlightsleep()に相当
}

void tud_resume_cb()
{
    // 直近のデータで再点灯
    for (int i = 0; i < TOTAL_LED_COUNT; i++) leds.setPixelColor(i, backup_colors[i]);
    leds.show();
}

uint16_t SendLampArrayAttributesReport(LampArrayAttributesReport *report)
{
    // 機器の物理情報を返す
    report->LampCount = TOTAL_LED_COUNT;
    report->BoundingBoxWidthInMicrometers = MILLIMETERS_TO_MICROMETERS(FAN_TOTAL_WIDTH_MM);
    report->BoundingBoxHeightInMicrometers = MILLIMETERS_TO_MICROMETERS(FAN_HEIGHT_MM);
    report->BoundingBoxDepthInMicrometers = MILLIMETERS_TO_MICROMETERS(FAN_DEPTH_MM);
    report->LampArrayKind = LampArrayKindChassis;
    report->MinUpdateIntervalInMicroseconds = MILLISECONDS_TO_MICROSECONDS(33);

    return sizeof(LampArrayAttributesReport);
}

void UpdateLampAttributes(LampAttributesRequestReport *report) noexcept
{
    // 対象となるLampId(0～LampCount-1)の受信。無効なLampIdは0として処理する
    last_lamp_id = (report->LampId < TOTAL_LED_COUNT) ? report->LampId : 0;
}

uint16_t SendLampAttributesReport(LampAttributesResponseReport *report) noexcept
{
    int fan_no = last_lamp_id / FAN_LED_COUNT;
    int led_no = last_lamp_id % FAN_LED_COUNT;

    float cx = fan_no * FAN_WIDTH_MM + FAN_WIDTH_MM / 2.0f;
    float cy = FAN_HEIGHT_MM / 2.0f;

    float x = cx + FAN_LED_POS_MM[led_no][0];
    float y = cy + FAN_LED_POS_MM[led_no][1];

    report->Attributes.LampId = last_lamp_id;
    report->Attributes.PositionXInMicrometers  = MILLIMETERS_TO_MICROMETERS(x);
    report->Attributes.PositionYInMicrometers  = MILLIMETERS_TO_MICROMETERS(y);
    report->Attributes.PositionZInMicrometers  = MILLIMETERS_TO_MICROMETERS(FAN_LED_DEPTH);
    report->Attributes.UpdateLatencyInMicroseconds = MILLISECONDS_TO_MICROSECONDS(LAMP_UPDATE_LATENCY);
    report->Attributes.LampPurposes = LampPurposeAccent;
    report->Attributes.RedLevelCount = 0xFF;
    report->Attributes.GreenLevelCount = 0xFF;
    report->Attributes.BlueLevelCount = 0xFF;
    report->Attributes.IntensityLevelCount = 0x01;
    report->Attributes.IsProgrammable = LAMP_IS_PROGRAMMABLE;
    report->Attributes.InputBinding  = 0x00;

    last_lamp_id++;
    if (last_lamp_id >= TOTAL_LED_COUNT) last_lamp_id = 0; // Reset

    return sizeof(LampAttributesResponseReport);
}

// デバイス設定の制御(AutonomousModeが無効ならデバイスでの制御はできず、有効ならデバイスでの変更が認められる)
void UpdateArrayControl(LampArrayControlReport *report) noexcept
{
    is_autonomous = !!report->AutonomousMode;
}

void setPixelColor(uint16_t id, uint32_t pxcolor)
{
    uint32_t old_color = leds.getPixelColor(id);
    if(old_color != pxcolor){
        leds.setPixelColor(id, pxcolor);
        require_update = true;
    }
}

// 複数のランプを一度に更新
void UpdateMultipleLamp(LampMultiUpdateReport *report) noexcept
{
    for (uint8_t i = 0; i < report->LampCount; i++){
        if (report->LampIds[i] < TOTAL_LED_COUNT) {
            LampArrayColor *c = &report->UpdateColors[i];
            uint32_t pxcolor = leds.Color(c->RedChannel, c->GreenChannel, c->BlueChannel);
            setPixelColor(report->LampIds[i], pxcolor);
        }
    }

    // ホストから送られるデータが最後ならこのフラグが立ち、次に「LampArrayControlReport(AutonomousMode: enabled)」が送信されることになる
    if (report->LampUpdateFlags & LAMP_UPDATE_FLAG_UPDATE_COMPLETE) is_completed = true;
}

// 2つのIDの範囲内のランプを更新
void UpdateRangeLamp(LampRangeUpdateReport *report) noexcept
{
    if (report->LampIdStart >= 0 && report->LampIdStart < TOTAL_LED_COUNT && 
        report->LampIdEnd >= 0 && report->LampIdEnd < TOTAL_LED_COUNT && 
        report->LampIdStart <= report->LampIdEnd)
    {
        for (uint8_t i = report->LampIdStart; i <= report->LampIdEnd; i++) {
            uint32_t pxcolor = leds.Color(report->UpdateColor.RedChannel, report->UpdateColor.GreenChannel, report->UpdateColor.BlueChannel);
            setPixelColor(i, pxcolor);
        }
    }

    // UpdateMultipleLampと同様、ホストから送られるデータが最後ならこのフラグが立つ
    if (report->LampUpdateFlags & LAMP_UPDATE_FLAG_UPDATE_COMPLETE) is_completed = true;
}

uint16_t OnGetReport(uint8_t report_id, hid_report_type_t report_type, uint8_t* data, uint16_t length) 
{
    uint16_t send_size = 0;

    switch (report_id){
    case LAMP_ARRAY_ATTRIBUTES: // 1: 機具の情報を返す。一番最初にホストから送られる。
        send_size = SendLampArrayAttributesReport((LampArrayAttributesReport*)data);
        break;

    case LAMP_ATTRIBUTES_RESPONSE: // 3: 要求されたIDに対応するランプの属性を返す
        send_size = SendLampAttributesReport((LampAttributesResponseReport*)data);
        break;
    }

    return send_size;
}

void OnSetReport(uint8_t report_id, hid_report_type_t report_type, uint8_t const* data, uint16_t length) 
{
    switch (report_id){
    case LAMP_ATTRIBUTES_REQUEST: // 2: LampIdの定義を返す
        UpdateLampAttributes((LampAttributesRequestReport*)data);
        break;

    // LAMP_ARRAY_CONTROLの後に送られる
    case LAMP_MULTI_UPDATE: // 4
        UpdateMultipleLamp((LampMultiUpdateReport*)data);
        break;
    case LAMP_RANGE_UPDATE: // 5
        UpdateRangeLamp((LampRangeUpdateReport*)data);
        break;

    case LAMP_ARRAY_CONTROL: // 6 : コントロールモードの変更要求
        UpdateArrayControl((LampArrayControlReport*)data);
        break;
    }
}