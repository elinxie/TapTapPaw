#include "accessory.h"
#include "animation.h"
#include "esp32-hal.h"
#include "ui/ui.h"


Preferences pref;
bool flip = false;

//---------------- Sounding -------------------------

uint8_t volume = 80;
bool sound_on = true;
uint8_t rotation = 0;

void initSpeaker() {
  uint16_t freq = 2000; // Hz
  uint16_t resolution = 10; // bits (0–1023)
  ledcSetup(SPEAKER_CH, freq, resolution);
  ledcAttachPin(SPEAKER_PIN, SPEAKER_CH);
}

// playToneWithVolume(2000, 20, 200);  // 20% volume, 2000 Hz, 200ms
void playToneWithVolume(uint16_t freq, uint8_t volumePercent, uint16_t duration) {
  if (!sound_on) return;

  // clamp volume between 0–100
  volumePercent = constrain(volumePercent, 0, 100);
  // convert percent to duty cycle
  int maxDuty = (1 << 10) - 1; // for 10-bit = 1023
  int duty = (maxDuty * volumePercent) / 100;
  ledcWriteTone(SPEAKER_CH, freq); // set frequency
  ledcWrite(SPEAKER_CH, duty); // apply duty cycle (volume)
  delay(duration);
  ledcWrite(SPEAKER_CH, 0); // stop

}

void beepbeep() { // 2 shot beep
  //initSpeaker();
  playToneWithVolume(2000, volume, 50);
  delay(50);
  playToneWithVolume(2000, volume, 50);
}
void beep() {
 // initSpeaker();
  playToneWithVolume(2000, volume, 250);
}
void clickSound() {//keyboard click sound
 // initSpeaker();
  playToneWithVolume(8000, volume, 1);
  

}
void offSound() {
 // initSpeaker();
  playToneWithVolume(3136, volume, 50);
  playToneWithVolume(2637, volume, 50);
  playToneWithVolume(2093, volume, 50);
}

//-----------  auto dim backlight function ------------

uint8_t dim_brightness = 30;
uint8_t brightness = 127;

bool dim = false; // back light dim flag
static const uint16_t LIGHT_LEVEL = 2000; // backlight control by light level Higher = darker light
unsigned long dimTimer = 0;

void autoDim() {
  if (millis() - dimTimer > 3000) {
    int light = analogRead(LDR_PIN); // read light
    // Serial.printf("LDR: %d\n",light);
    if (light > LIGHT_LEVEL) { // low light
        if (!dim) {
          tft.setBrightness(dim_brightness); // dim light
          dim = true;
        } // if !dim 
    } else {
        if (dim) {
          tft.setBrightness(brightness);
          dim = false;
        } // if dim
    } // if light > lightlevel
    dimTimer = millis();
  }
} // autodim

// led_onoff function
void led_color(bool r, bool g, bool b  ) {
  digitalWrite(LED_RED_PIN, r);
  digitalWrite(LED_GREEN_PIN, g);
  digitalWrite(LED_BLUE_PIN, b);
}


//------------- configuration ---------------------
void load_config() {
  pref.begin("config", false);
 
  flip = pref.getBool("flip", false);
  if (flip) {
    lv_obj_add_state(ui_setting_Switch_rotate, LV_STATE_CHECKED);
    rotation = 2;
  } else {
    lv_obj_clear_state(ui_setting_Switch_rotate, LV_STATE_CHECKED); 
    rotation = 0;
  }
  tft.setRotation(rotation);
  
  dim_brightness = pref.getUChar("dim_brightness", 30);
  brightness = pref.getUChar("brightness", 200);
  lv_slider_set_left_value(ui_setting_Slider_backlight , dim_brightness, LV_ANIM_OFF);
  lv_slider_set_value(ui_setting_Slider_backlight, brightness, LV_ANIM_OFF);
  tft.setBrightness(brightness);

  volume = pref.getUChar("volume", 80);
  lv_slider_set_value(ui_setting_Slider_volume, volume, LV_ANIM_OFF);

  sound_on = pref.getBool("sound_on", true);
  if (sound_on) {
    lv_obj_add_state(ui_setting_Switch_sound, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(ui_setting_Switch_sound      , LV_STATE_CHECKED);
  } 

  pref.end();
}

void save_config() {
  pref.begin("config", false);
  pref.putBool("flip", flip);
  pref.putUChar("dim_brightness", dim_brightness);
  pref.putUChar("brightness", brightness);
  pref.putUChar("volume", volume);
  pref.putBool("sound_on", sound_on); 
  pref.end();
  beep();
}

//--------------- LittleFS for LVGL --------------
//init lvgl littlefs drive S:
static void * littlefs_open_cb(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode) {
    const char * flags = "r";

    if (mode == LV_FS_MODE_WR) {
        flags = "w";
    } else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) {
        flags = "r+";
    }

    String full_path;

    if (path[0] == '/') {
        full_path = path;
    } else {
        full_path = "/";
        full_path += path;
    }

    File f = LittleFS.open(full_path, flags);

    if (!f) {
        log_e("LittleFS.open FAILED: ");
        log_e("%s", full_path.c_str());
        return NULL;
    }

    File * fp = new File(f);
    return fp;
}

static lv_fs_res_t littlefs_close_cb(lv_fs_drv_t * drv, void * file_p) {
    File * fp = (File *)file_p;
    fp->close();
    delete fp;
    return LV_FS_RES_OK;
}

static lv_fs_res_t littlefs_read_cb(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br) {
    File * fp = (File *)file_p;
    *br = fp->read((uint8_t *)buf, btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t littlefs_seek_cb(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence) {
    File * fp = (File *)file_p;

    SeekMode mode = SeekSet;

    if (whence == LV_FS_SEEK_CUR) {
        mode = SeekCur;
    } else if (whence == LV_FS_SEEK_END) {
        mode = SeekEnd;
    }

    bool ok = fp->seek(pos, mode);
    return ok ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t littlefs_tell_cb(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p) {
    File * fp = (File *)file_p;
    *pos_p = fp->position();
    return LV_FS_RES_OK;
}


void lvgl_littlefs_init() {
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);

    drv.letter = 'S';
    drv.open_cb = littlefs_open_cb;
    drv.close_cb = littlefs_close_cb;
    drv.read_cb = littlefs_read_cb;
    drv.seek_cb = littlefs_seek_cb;
    drv.tell_cb = littlefs_tell_cb;

    lv_fs_drv_register(&drv);

    log_d("LVGL LittleFS registered as S:");
}

// init LittleFS
void initLittleFS() {
    if (!LittleFS.begin(false)) {
        log_e("LittleFS mount failed. Formatting...");

        if (!LittleFS.begin(true)) {
            log_e("LittleFS format failed!");
        }

        log_d("LittleFS formatted successfully.");
    } else {
        log_d("LittleFS mounted successfully.");
    }

    //lvgl_littlefs_init();   // register LVGL drive S:
}
//--------------------------------------
