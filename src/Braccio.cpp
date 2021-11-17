#include "Braccio++.h"
#include "splash.h"
//#include "menu.impl"

#if LVGL_VERSION_MAJOR < 8 || (LVGL_VERSION_MAJOR == 8 && LVGL_VERSION_MINOR < 1)
#error Please use lvgl >= 8.1
#endif

#include "mbed.h"

void my_print( const char * dsc )
{
    Serial.println(dsc);
}

using namespace std::chrono_literals;

bool BraccioClass::begin(voidFuncPtr customMenu) {

	Wire.begin();
	Serial.begin(115200);

	pinMode(PIN_FUSB302_INT, INPUT_PULLUP);

#ifdef __MBED__
	static rtos::Thread th(osPriorityHigh);
	th.start(mbed::callback(this, &BraccioClass::pd_thread));
	attachInterrupt(PIN_FUSB302_INT, mbed::callback(this, &BraccioClass::unlock_pd_semaphore_irq), FALLING);
	pd_timer.attach(mbed::callback(this, &BraccioClass::unlock_pd_semaphore), 10ms);
#endif

	PD_UFP.init_PPS(PPS_V(7.2), PPS_A(2.0));

/*
	while (millis() < 200) {
		PD_UFP.run();
	}
*/

	pinMode(1, INPUT_PULLUP);

	SPI.begin();

	i2c_mutex.lock();
	bl.begin();
	if (bl.getChipID() != 0xCE) {
		return false;
	}
	bl.setColor(red);

	int ret = expander.testConnection();

	if (ret == false) {
		return ret;
	}

	for (int i = 0; i < 14; i++) {
		expander.setPinDirection(i, 0);
	}

	// Set SLEW to low
	expander.setPinDirection(21, 0); // P25 = 8 * 2 + 5
	expander.writePin(21, 0);

	// Set TERM to HIGH (default)
	expander.setPinDirection(19, 0); // P23 = 8 * 2 + 3
	expander.writePin(19, 1);

	expander.setPinDirection(18, 0); // P22 = 8 * 2 + 2
	expander.writePin(18, 0); // reset LCD
	expander.writePin(18, 1); // LCD out of reset
	i2c_mutex.unlock();

	pinMode(BTN_LEFT, INPUT_PULLUP);
	pinMode(BTN_RIGHT, INPUT_PULLUP);
	pinMode(BTN_UP, INPUT_PULLUP);
	pinMode(BTN_DOWN, INPUT_PULLUP);
	pinMode(BTN_SEL, INPUT_PULLUP);
	pinMode(BTN_ENTER, INPUT_PULLUP);

#if LV_USE_LOG
	lv_log_register_print_cb( my_print );
#endif

  lv_init();

  lv_disp_draw_buf_init(&disp_buf, buf, NULL, 240 * 240 / 10);

  /*Initialize the display*/
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 240;
  disp_drv.ver_res = 240;
  disp_drv.flush_cb = braccio_disp_flush;
  disp_drv.draw_buf = &disp_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_KEYPAD;
  indev_drv.read_cb = read_keypad;
  kb_indev = lv_indev_drv_register(&indev_drv);

	gfx.init();
	gfx.setRotation(4);
	gfx.fillScreen(TFT_BLACK);
	gfx.setAddrWindow(0, 0, 240, 240);
	gfx.setFreeFont(&FreeSans18pt7b);

	gfx.drawBitmap(44, 60, ArduinoLogo, 152, 72, 0x04B3);
  gfx.drawBitmap(48, 145, ArduinoText, 144, 23, 0x04B3);

  delay(2000);

  p_objGroup = lv_group_create();
  lv_group_set_default(p_objGroup);

  if (customMenu) {
    customMenu();
  } else {
    defaultMenu();
  }

	gfx.fillScreen(TFT_BLACK);
	gfx.println("\n\nPlease\nconnect\npower");

	PD_UFP.print_status(Serial);
	while (!PD_UFP.is_PPS_ready()) {
		i2c_mutex.lock();
		PD_UFP.print_status(Serial);
		//PD_UFP.print_status(Serial);
		PD_UFP.init_PPS(PPS_V(7.2), PPS_A(2.0));
		i2c_mutex.unlock();
	}

#ifdef __MBED__
	static rtos::Thread display_th;
	display_th.start(mbed::callback(this, &BraccioClass::display_thread));
#endif

	servos->begin();
	servos->setPositionMode(pmIMMEDIATE);

#ifdef __MBED__
	static rtos::Thread connected_th;
	connected_th.start(mbed::callback(this, &BraccioClass::motors_connected_thread));
#endif

	return true;
}

void BraccioClass::connectJoystickTo(lv_obj_t* obj) {
  lv_group_add_obj(p_objGroup, obj);
  lv_indev_set_group(kb_indev, p_objGroup);
}

void BraccioClass::pd_thread() {
	start_pd_burst = millis();
  while (1) {
    auto ret = pd_events.wait_any(0xFF);
    if ((ret & 1) && (millis() - start_pd_burst > 1000)) {
      pd_timer.detach();
      pd_timer.attach(mbed::callback(this, &BraccioClass::unlock_pd_semaphore), 1s);
    }
    if (ret & 2) {
      pd_timer.detach();
      pd_timer.attach(mbed::callback(this, &BraccioClass::unlock_pd_semaphore), 50ms);
    }
    i2c_mutex.lock();
    PD_UFP.run();
    i2c_mutex.unlock();
    if (PD_UFP.is_power_ready() && PD_UFP.is_PPS_ready()) {

    }
  }
}

void BraccioClass::display_thread() {
  while (1) {
  	/*
    if ((braccio::encoder.menu_running) && (braccio::encoder.menu_interrupt)) {
      braccio::encoder.menu_interrupt = false;
      braccio::nav.doInput();
      braccio::nav.doOutput();
    }
    yield();
    */
    lv_task_handler();
    lv_tick_inc(LV_DISP_DEF_REFR_PERIOD);
    delay(30);
  }
}

#include <extra/libs/gif/lv_gif.h>

void BraccioClass::defaultMenu() {

	lv_obj_t* welcomemessage = lv_label_create(lv_scr_act());
	lv_label_set_long_mode(welcomemessage, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_obj_set_width(welcomemessage, lv_disp_get_hor_res( NULL ) / 2);
	lv_label_set_text(welcomemessage, "ARDUINO BRACCIO ++ ");
	lv_obj_align(welcomemessage, LV_ALIGN_CENTER, 50, 0);

	LV_IMG_DECLARE(img_bulb_gif);
	lv_obj_t* img = lv_gif_create(lv_scr_act());
	lv_gif_set_src(img, &img_bulb_gif);
	lv_obj_align(img, LV_ALIGN_LEFT_MID, 20, 0);
}

void BraccioClass::motors_connected_thread() {
  while (1) {
    if (ping_allowed) {
	    for (int i = 1; i < 7; i++) {
	      _connected[i] = (servos->ping(i) == 0);
	      //Serial.print(String(i) + ": ");
	      //Serial.println(_connected[i]);
	      i2c_mutex.lock();
	      if (_connected[i]) {
	        setGreen(i);
	      } else {
	        setRed(i);
	      }
	      i2c_mutex.unlock();
	    }
	  }
    delay(1000);
  }
}

int BraccioClass::getKey() {
	if (digitalRead(BTN_LEFT) == LOW) {
		return 1;
	}
	if (digitalRead(BTN_RIGHT) == LOW) {
		return 2;
	}
	if (digitalRead(BTN_SEL) == LOW) {
		return 3;
	}
	if (digitalRead(BTN_UP) == LOW) {
		return 4;
	}
	if (digitalRead(BTN_DOWN) == LOW) {
		return 5;
	}
	if (digitalRead(BTN_ENTER) == LOW) {
		return 6;
	}
	return 0;
}

BraccioClass Braccio;

/* Display flushing */
extern "C" void braccio_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  Braccio.gfx.startWrite();
  Braccio.gfx.setAddrWindow(area->x1, area->y1, w, h);
  Braccio.gfx.pushColors(&color_p->full, w * h, true);
  Braccio.gfx.endWrite();

  lv_disp_flush_ready(disp);
}

/* Reading input device (simulated encoder here) */
extern "C" void read_keypad(lv_indev_drv_t * drv, lv_indev_data_t* data)
{
    static uint32_t last_key = 0;

    /*Get whether the a key is pressed and save the pressed key*/
    uint32_t act_key = Braccio.getKey();
    if(act_key != 0) {
        data->state = LV_INDEV_STATE_PR;

        /*Translate the keys to LVGL control characters according to your key definitions*/
        switch(act_key) {
        case 4:
            act_key = LV_KEY_UP;
            break;
        case 5:
            act_key = LV_KEY_DOWN;
            break;
        case 6:
            act_key = LV_KEY_HOME;
            break;
        case 1:
            act_key = LV_KEY_LEFT;
            break;
        case 2:
            act_key = LV_KEY_RIGHT;
            break;
        case 3:
            act_key = LV_KEY_ENTER;
            break;
        }

        last_key = act_key;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

    data->key = last_key;
}
