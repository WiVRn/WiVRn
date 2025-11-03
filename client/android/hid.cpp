#include "hid.h"

#include "scene.h"
#include <spdlog/spdlog.h>

#define NUM_KEYCODES 226
/// Maps Android KeyCodes to Linux key event codes
static const int KEYCODE_TO_VK[NUM_KEYCODES] = {
        0,         // KEYCODE_UNKNOWN
        0,         // KEYCODE_SOFT_LEFT
        0,         // KEYCODE_SOFT_RIGHT
        0,         // KEYCODE_HOME
        0,         // KEYCODE_BACK
        0,         // KEYCODE_CALL
        0,         // KEYCODE_ENDCALL
        KEY_0,     // KEYCODE_0
        KEY_1,     // KEYCODE_1
        KEY_2,     // KEYCODE_2
        KEY_3,     // KEYCODE_3
        KEY_4,     // KEYCODE_4
        KEY_5,     // KEYCODE_5
        KEY_6,     // KEYCODE_6
        KEY_7,     // KEYCODE_7
        KEY_8,     // KEYCODE_8
        KEY_9,     // KEYCODE_9
        0,         // KEYCODE_STAR
        0,         // KEYCODE_POUND
        KEY_UP,    // KEYCODE_DPAD_UP
        KEY_DOWN,  // KEYCODE_DPAD_DOWN
        KEY_LEFT,  // KEYCODE_DPAD_LEFT
        KEY_RIGHT, // KEYCODE_DPAD_RIGHT
        0,         // KEYCODE_DPAD_CENTER

        // there's no way to discern between BT keyboard & device buttons using NDK,
        // so having these mapped consumes all events from the HMD volume rocker.
        // alternatively, move the handling to stream.cpp
        0, // KEYCODE_VOLUME_UP // KEY_VOLUMEUP
        0, // KEYCODE_VOLUME_DOWN // KEY_VOLUMEDOWN

        0,                    // KEYCODE_POWER - device power button, do not consume
        0,                    // KEYCODE_CAMERA
        0,                    // KEYCODE_CLEAR
        KEY_A,                // KEYCODE_A
        KEY_B,                // KEYCODE_B
        KEY_C,                // KEYCODE_C
        KEY_D,                // KEYCODE_D
        KEY_E,                // KEYCODE_E
        KEY_F,                // KEYCODE_F
        KEY_G,                // KEYCODE_G
        KEY_H,                // KEYCODE_H
        KEY_I,                // KEYCODE_I
        KEY_J,                // KEYCODE_J
        KEY_K,                // KEYCODE_K
        KEY_L,                // KEYCODE_L
        KEY_M,                // KEYCODE_M
        KEY_N,                // KEYCODE_N
        KEY_O,                // KEYCODE_O
        KEY_P,                // KEYCODE_P
        KEY_Q,                // KEYCODE_Q
        KEY_R,                // KEYCODE_R
        KEY_S,                // KEYCODE_S
        KEY_T,                // KEYCODE_T
        KEY_U,                // KEYCODE_U
        KEY_V,                // KEYCODE_V
        KEY_W,                // KEYCODE_W
        KEY_X,                // KEYCODE_X
        KEY_Y,                // KEYCODE_Y
        KEY_Z,                // KEYCODE_Z
        KEY_COMMA,            // KEYCODE_COMMA
        KEY_DOT,              // KEYCODE_PERIOD
        KEY_LEFTALT,          // KEYCODE_ALT_LEFT
        KEY_RIGHTALT,         // KEYCODE_ALT_RIGHT
        KEY_LEFTSHIFT,        // KEYCODE_SHIFT_LEFT
        KEY_RIGHTSHIFT,       // KEYCODE_SHIFT_RIGHT
        KEY_TAB,              // KEYCODE_TAB
        KEY_SPACE,            // KEYCODE_SPACE
        0,                    // KEYCODE_SYM
        KEY_WWW,              // KEYCODE_EXPLORER
        KEY_MAIL,             // KEYCODE_ENVELOPE
        KEY_ENTER,            // KEYCODE_ENTER
        KEY_BACKSPACE,        // KEYCODE_DEL
        KEY_GRAVE,            // KEYCODE_GRAVE
        KEY_MINUS,            // KEYCODE_MINUS
        KEY_EQUAL,            // KEYCODE_EQUALS
        KEY_LEFTBRACE,        // KEYCODE_LEFT_BRACKET
        KEY_RIGHTBRACE,       // KEYCODE_RIGHT_BRACKET
        KEY_BACKSLASH,        // KEYCODE_BACKSLASH
        KEY_SEMICOLON,        // KEYCODE_SEMICOLON
        KEY_APOSTROPHE,       // KEYCODE_APOSTROPHE
        KEY_SLASH,            // KEYCODE_SLASH
        0,                    // KEYCODE_AT
        0,                    // KEYCODE_NUM
        0,                    // KEYCODE_HEADSETHOOK
        0,                    // KEYCODE_FOCUS
        KEY_EQUAL,            // KEYCODE_PLUS
        KEY_MENU,             // KEYCODE_MENU
        0,                    // KEYCODE_NOTIFICATION
        KEY_SEARCH,           // KEYCODE_SEARCH
        KEY_PLAYPAUSE,        // KEYCODE_MEDIA_PLAY_PAUSE
        KEY_STOPCD,           // KEYCODE_MEDIA_STOP
        KEY_NEXTSONG,         // KEYCODE_MEDIA_NEXT
        KEY_PREVIOUSSONG,     // KEYCODE_MEDIA_PREVIOUS
        KEY_REWIND,           // KEYCODE_MEDIA_REWIND
        KEY_FASTFORWARD,      // KEYCODE_MEDIA_FAST_FORWARD
        KEY_MUTE,             // KEYCODE_MUTE
        KEY_PAGEUP,           // KEYCODE_PAGE_UP
        KEY_PAGEDOWN,         // KEYCODE_PAGE_DOWN
        0,                    // KEYCODE_PICTSYMBOLS
        0,                    // KEYCODE_SWITCH_CHARSET
        0,                    // KEYCODE_BUTTON_A
        0,                    // KEYCODE_BUTTON_B
        0,                    // KEYCODE_BUTTON_C
        0,                    // KEYCODE_BUTTON_X
        0,                    // KEYCODE_BUTTON_Y
        0,                    // KEYCODE_BUTTON_Z
        0,                    // KEYCODE_BUTTON_L1
        0,                    // KEYCODE_BUTTON_R1
        0,                    // KEYCODE_BUTTON_L2
        0,                    // KEYCODE_BUTTON_R2
        0,                    // KEYCODE_BUTTON_THUMBL
        0,                    // KEYCODE_BUTTON_THUMBR
        0,                    // KEYCODE_BUTTON_START
        0,                    // KEYCODE_BUTTON_SELECT
        0,                    // KEYCODE_BUTTON_MODE
        KEY_ESC,              // KEYCODE_ESCAPE
        KEY_DELETE,           // KEYCODE_FORWARD_DEL
        KEY_LEFTCTRL,         // KEYCODE_CTRL_LEFT
        KEY_RIGHTCTRL,        // KEYCODE_CTRL_RIGHT
        KEY_CAPSLOCK,         // KEYCODE_CAPS_LOCK
        KEY_SCROLLLOCK,       // KEYCODE_SCROLL_LOCK
        KEY_LEFTMETA,         // KEYCODE_META_LEFT
        KEY_RIGHTMETA,        // KEYCODE_META_RIGHT
        0,                    // KEYCODE_FUNCTION
        KEY_SYSRQ,            // KEYCODE_SYSRQ
        KEY_PAUSE,            // KEYCODE_BREAK
        KEY_HOME,             // KEYCODE_MOVE_HOME
        KEY_END,              // KEYCODE_MOVE_END
        KEY_INSERT,           // KEYCODE_INSERT
        KEY_FORWARD,          // KEYCODE_FORWARD
        KEY_PLAYCD,           // KEYCODE_MEDIA_PLAY
        KEY_PAUSECD,          // KEYCODE_MEDIA_PAUSE
        KEY_CLOSECD,          // KEYCODE_MEDIA_CLOSE
        KEY_EJECTCD,          // KEYCODE_MEDIA_EJECT
        KEY_RECORD,           // KEYCODE_MEDIA_RECORD
        KEY_F1,               // KEYCODE_F1
        KEY_F2,               // KEYCODE_F2
        KEY_F3,               // KEYCODE_F3
        KEY_F4,               // KEYCODE_F4
        KEY_F5,               // KEYCODE_F5
        KEY_F6,               // KEYCODE_F6
        KEY_F7,               // KEYCODE_F7
        KEY_F8,               // KEYCODE_F8
        KEY_F9,               // KEYCODE_F9
        KEY_F10,              // KEYCODE_F10
        KEY_F11,              // KEYCODE_F11
        KEY_F12,              // KEYCODE_F12
        KEY_NUMLOCK,          // KEYCODE_NUM_LOCK
        KEY_KP0,              // KEYCODE_NUMPAD_0
        KEY_KP1,              // KEYCODE_NUMPAD_1
        KEY_KP2,              // KEYCODE_NUMPAD_2
        KEY_KP3,              // KEYCODE_NUMPAD_3
        KEY_KP4,              // KEYCODE_NUMPAD_4
        KEY_KP5,              // KEYCODE_NUMPAD_5
        KEY_KP6,              // KEYCODE_NUMPAD_6
        KEY_KP7,              // KEYCODE_NUMPAD_7
        KEY_KP8,              // KEYCODE_NUMPAD_8
        KEY_KP9,              // KEYCODE_NUMPAD_9
        KEY_KPSLASH,          // KEYCODE_NUMPAD_DIVIDE
        KEY_KPASTERISK,       // KEYCODE_NUMPAD_MULTIPLY
        KEY_KPMINUS,          // KEYCODE_NUMPAD_SUBTRACT
        KEY_KPPLUS,           // KEYCODE_NUMPAD_ADD
        KEY_KPDOT,            // KEYCODE_NUMPAD_DOT
        KEY_KPCOMMA,          // KEYCODE_NUMPAD_COMMA
        KEY_KPENTER,          // KEYCODE_NUMPAD_ENTER
        KEY_KPEQUAL,          // KEYCODE_NUMPAD_EQUALS
        KEY_KPLEFTPAREN,      // KEYCODE_NUMPAD_LEFT_PAREN
        KEY_KPRIGHTPAREN,     // KEYCODE_NUMPAD_RIGHT_PAREN
        KEY_MUTE,             // KEYCODE_VOLUME_MUTE
        0,                    // KEYCODE_INFO
        0,                    // KEYCODE_CHANNEL_UP
        0,                    // KEYCODE_CHANNEL_DOWN
        0,                    // KEYCODE_ZOOM_IN
        0,                    // KEYCODE_ZOOM_OUT
        0,                    // KEYCODE_TV
        0,                    // KEYCODE_WINDOW
        0,                    // KEYCODE_GUIDE
        0,                    // KEYCODE_DVR
        KEY_BOOKMARKS,        // KEYCODE_BOOKMARK
        0,                    // KEYCODE_CAPTIONS
        0,                    // KEYCODE_SETTINGS
        0,                    // KEYCODE_TV_POWER
        0,                    // KEYCODE_TV_INPUT
        0,                    // KEYCODE_STB_POWER
        0,                    // KEYCODE_STB_INPUT
        0,                    // KEYCODE_AVR_POWER
        0,                    // KEYCODE_AVR_INPUT
        0,                    // KEYCODE_PROG_RED
        0,                    // KEYCODE_PROG_GREEN
        0,                    // KEYCODE_PROG_YELLOW
        0,                    // KEYCODE_PROG_BLUE
        0,                    // KEYCODE_APP_SWITCH
        0,                    // KEYCODE_BUTTON_1
        0,                    // KEYCODE_BUTTON_2
        0,                    // KEYCODE_BUTTON_3
        0,                    // KEYCODE_BUTTON_4
        0,                    // KEYCODE_BUTTON_5
        0,                    // KEYCODE_BUTTON_6
        0,                    // KEYCODE_BUTTON_7
        0,                    // KEYCODE_BUTTON_8
        0,                    // KEYCODE_BUTTON_9
        0,                    // KEYCODE_BUTTON_10
        0,                    // KEYCODE_BUTTON_11
        0,                    // KEYCODE_BUTTON_12
        0,                    // KEYCODE_BUTTON_13
        0,                    // KEYCODE_BUTTON_14
        0,                    // KEYCODE_BUTTON_15
        0,                    // KEYCODE_BUTTON_16
        KEY_LANGUAGE,         // KEYCODE_LANGUAGE_SWITCH
        0,                    // KEYCODE_MANNER_MODE
        0,                    // KEYCODE_3D_MODE
        0,                    // KEYCODE_CONTACTS
        KEY_CALENDAR,         // KEYCODE_CALENDAR
        0,                    // KEYCODE_MUSIC
        KEY_CALC,             // KEYCODE_CALCULATOR
        KEY_ZENKAKUHANKAKU,   // KEYCODE_ZENKAKU_HANKAKU
        0,                    // KEYCODE_EISU
        KEY_MUHENKAN,         // KEYCODE_MUHENKAN
        KEY_HENKAN,           // KEYCODE_HENKAN
        KEY_KATAKANAHIRAGANA, // KEYCODE_KATAKANA_HIRAGANA
        KEY_YEN,              // KEYCODE_YEN
        KEY_RO,               // KEYCODE_RO
        KEY_KATAKANAHIRAGANA, // KEYCODE_KANA
        KEY_ASSISTANT,        // KEYCODE_ASSIST
        KEY_BRIGHTNESSDOWN,   // KEYCODE_BRIGHTNESS_DOWN
        KEY_BRIGHTNESSUP,     // KEYCODE_BRIGHTNESS_UP
        0,                    // KEYCODE_MEDIA_AUDIO_TRACK
        KEY_SLEEP,            // KEYCODE_SLEEP
        KEY_WAKEUP,           // KEYCODE_WAKEUP
};

uint8_t key_code_to_vk(int16_t key_code)
{
	if (key_code < 0 || key_code >= NUM_KEYCODES)
		return 0;
	return KEYCODE_TO_VK[key_code];
}

bool android_hid::input_handler::handle_input(scene * current_scene, AInputEvent * event)
{
	const int32_t type = AInputEvent_getType(event);
	const int32_t src = AInputEvent_getSource(event);
	const int32_t dev = AInputEvent_getDeviceId(event);

	if (type == AINPUT_EVENT_TYPE_KEY)
	{
		int32_t flags = AKeyEvent_getFlags(event);
		if ((flags & AKEY_EVENT_FLAG_SOFT_KEYBOARD) != 0)
			return false; // ignore soft keyboards

		if ((src & AINPUT_SOURCE_KEYBOARD) == 0)
			return false; // event is not from keyboard

		int32_t keycode = AKeyEvent_getKeyCode(event);
		int32_t action = AKeyEvent_getAction(event);

		bool pressed;
		switch (action)
		{
			case AKEY_EVENT_ACTION_DOWN:
				pressed = true;
				break;
			case AKEY_EVENT_ACTION_UP:
				pressed = false;
				break;
			default:
				return false;
		}

		uint8_t vk = key_code_to_vk(keycode);
		if (vk == 0)
			return false;

		if (pressed)
			return current_scene->on_input_key_down(vk);
		else
			return current_scene->on_input_key_up(vk);
	}

	if (type == AINPUT_EVENT_TYPE_MOTION)
	{
		// don't care about absolute mouse due to mapping difficulties
		if ((src & AINPUT_SOURCE_MOUSE_RELATIVE) == 0)
			return false;

		int32_t actionMasked = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
		switch (actionMasked)
		{
			case AMOTION_EVENT_ACTION_HOVER_MOVE: // mouse move with no buttons pressed
			case AMOTION_EVENT_ACTION_MOVE: {     // mouse move with buttons pressed
				float x = AMotionEvent_getX(event, 0);
				float y = AMotionEvent_getY(event, 0);
				return current_scene->on_input_mouse_move(x, y);
			}
			case AMOTION_EVENT_ACTION_BUTTON_PRESS:
			case AMOTION_EVENT_ACTION_BUTTON_RELEASE: {
				uint32_t buttons = (uint32_t)AMotionEvent_getButtonState(event);
				auto button_diff = buttons ^ buttons_before;
				bool ret_val = false;

				for (uint8_t i = 0; i < 3; ++i)
				{
					auto i_shifted = (1u << i);
					if ((button_diff & i_shifted) != 0)
					{
						if ((buttons & i_shifted) != 0)
							ret_val = current_scene->on_input_button_down(i);
						else
							ret_val = current_scene->on_input_button_up(i);
					}
				}

				buttons_before = buttons;
				return ret_val;
			}
			case AMOTION_EVENT_ACTION_SCROLL: {
				float h = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0);
				float v = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
				return current_scene->on_input_scroll(h, v);
			}
			default:
				return false;
		}
	}

	return false;
}

bool try_clear_exception(JNIEnv * env, const char * where)
{
	if (env->ExceptionCheck())
	{
		spdlog::error("[pointer_capture] Exception at {}", where);
		env->ExceptionDescribe(); // logs to logcat
		env->ExceptionClear();
		return true;
	}
	return false;
}

void make_decor_view_call(ANativeActivity * activity, const char * method_name)
{
	if (!activity || !activity->vm || !activity->env || !activity->clazz)
	{
		spdlog::error("[pointer_capture] Invalid ANativeActivity* or missing VM/Activity");
		return;
	}

	JNIEnv * env = activity->env;
	jobject act = activity->clazz;
	jclass clsActivity = env->GetObjectClass(act);
	jclass clsWindow = 0;
	jobject window = 0;
	jclass clsView = 0;
	jobject decor = 0;

	do
	{
		if (try_clear_exception(env, "GetObjectClass(Activity)") || not clsActivity)
			break; // goto cleanup

		jmethodID midGetWindow = env->GetMethodID(clsActivity, "getWindow", "()Landroid/view/Window;");
		if (try_clear_exception(env, "Activity.getWindow() method lookup") || not midGetWindow)
			break; // goto cleanup

		window = env->CallObjectMethod(act, midGetWindow);
		if (try_clear_exception(env, "Activity.getWindow() call") || not window)
			break; // goto cleanup

		clsWindow = env->GetObjectClass(window);
		if (try_clear_exception(env, "GetObjectClass(Window)") || not clsWindow)
			break; // goto cleanup

		jmethodID midGetDecor = env->GetMethodID(clsWindow, "getDecorView", "()Landroid/view/View;");
		if (try_clear_exception(env, "Window.getDecorView() method lookup") || not midGetDecor)
			break; // goto cleanup

		decor = env->CallObjectMethod(window, midGetDecor);
		if (try_clear_exception(env, "Window.getDecorView() call") || not decor)
			break; // goto cleanup

		clsView = env->GetObjectClass(decor);
		if (try_clear_exception(env, "GetObjectClass(DecorView)") || not clsView)
			break; // goto cleanup

		jmethodID midAction = env->GetMethodID(clsView, method_name, "()V");
		if (try_clear_exception(env, "View method lookup") || not midAction)
			break; // goto cleanup

		env->CallVoidMethod(decor, midAction);
		if (try_clear_exception(env, "View method call"))
			break; // goto cleanup

		spdlog::info("[pointer_capture] {}() invoked successfully", method_name);
	} while (false);

	// cleanup
	if (clsView)
		env->DeleteLocalRef(clsView);
	if (decor)
		env->DeleteLocalRef(decor);
	if (clsWindow)
		env->DeleteLocalRef(clsWindow);
	if (window)
		env->DeleteLocalRef(window);
	if (clsActivity)
		env->DeleteLocalRef(clsActivity);
}

void android_hid::request_pointer_capture(ANativeActivity * activity)
{
	make_decor_view_call(activity, "requestPointerCapture");
}

void android_hid::release_pointer_capture(ANativeActivity * activity)
{
	make_decor_view_call(activity, "releasePointerCapture");
}
