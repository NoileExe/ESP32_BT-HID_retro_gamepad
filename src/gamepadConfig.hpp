
/*
 * ESP32_BT-HID_retro_gamepad
 * Firmware for a retro gamepad based on ESP32, emulating a Bluetooth HID device.
 * Copyright (C) 2026 Belobragin Anton Igorevich
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

//------------------------------------------------------------------

#pragma once


#include <Arduino.h>
#include <array>
#include <string_view>

//------------------------------------------------------------------

using TypeMS = decltype(millis());

//------------------------------------------------------------------

// ВЫБОР ТИПА ГЕЙМПАДА
#define ONLY_DIGITAL_GAMEPAD		// Только цифровые кнопки без аналоговых стиков/триггеров
									// 2ух кнопочные: NES, Sega Master System, TurboGrafx, atari 7800, Amstrad GX4000, ...
									// 4ех кнопочные: SNES, Neo Geo, Neo Geo CD, Vectrex, Philips CD-i, Amiga CD32, ...
									// 3ех/6ти кнопочные: Sega Saturn, Sega Genesis/MD2, Panasonic 3DO, ~Atari Jaguar, ~NEC PC-FX

//#define ANALOGUE_GAMEPAD			// Наличие аналоговых стиков и триггеров
									// PlayStation, Dreamcast, XBOX, GameCube, Wii / WiiU


// Проверка, что определён хотя бы один из вариантов геймпада
#if !defined(ONLY_DIGITAL_GAMEPAD) && !defined(ANALOGUE_GAMEPAD)
	#error "Define ONLY_DIGITAL_GAMEPAD, ANALOGUE_GAMEPAD!"
#endif

//------------------------------------------------------------------

// === Идентификационные данные BT-устройства  ===
inline constexpr std::string_view GAMEPAD_NAME = "My Gamepad 001";		// Имя BT-устройства
inline constexpr std::string_view GAMEPAD_MANUFACTURER = "NoileExe";	// Производитель BT-устройства

// Версионные данные
inline constexpr std::string_view MODEL_NUMBER = "ESP32_BT-HID_retro_gamepad 1.0";
inline constexpr std::string_view SERIAL_NUMBER = "001";		// Серийный номер устройства (увеличивать для каждого след. устройства)
inline constexpr std::string_view SOFTWARE_REVISION = "0.3.0";	// Версия прошивки
inline constexpr std::string_view HARDWARE_REVISION = "1.1";	// Ревизия платы/железа/конфигурации
// Версия библиотеки pioarduino
inline constexpr std::string_view FIRMWARE_REVISION = "Arduino v3.3.8 | ESP-IDF v5.5.4 (13.04.2026)";

//------------------------------------------------------------------

// === Конфигурация кнопок и осей геймпада  ===
inline constexpr uint16_t ALL_BUTTONS_COUNT = 16;	// A B C X Y Z L1 R1 L3 R3, но Start Select - относятся к специальным в D-Input (Generic) профиле
inline constexpr uint8_t HAT_SWITCHES_COUNT = 1;	// Количество D-Pad'ов

#ifdef ONLY_DIGITAL_GAMEPAD
	/*inline constexpr bool ENABLE_X = false;			// Ось X левого стика
	inline constexpr bool ENABLE_Y = false;			// Ось Y левого стика

	inline constexpr bool ENABLE_Z = false;			// Ось X правого стика
	inline constexpr bool ENABLE_RZ = false;		// Ось Y правого стика
	
	inline constexpr bool ENABLE_RX = false;		// Ось L2 левого триггера
	inline constexpr bool ENABLE_RY = false;		// Ось R2 правого триггера

	*/
	
	inline constexpr bool ENABLE_LTHUMB = false;	// Оси X/Y левого стика выключены
	inline constexpr bool ENABLE_RTHUMB = false;	// Оси X/Y правого стика выключены
	inline constexpr bool ENABLE_L2 = false;		// Ось L2 левого триггера выключена
	inline constexpr bool ENABLE_R2 = false;		// Ось R2 правого триггера выключена
#elifdef ANALOGUE_GAMEPAD
	inline constexpr bool ENABLE_LTHUMB = true;		// Оси X/Y левого стика включены
	inline constexpr bool ENABLE_RTHUMB = true;		// Оси X/Y правого стика включены
	inline constexpr bool ENABLE_L2 = true;			// Ось L2 левого триггера включена
	inline constexpr bool ENABLE_R2 = true;			// Ось R2 правого триггера включена
#endif

inline constexpr bool ENABLE_SLIDER1 = false;		// Доп. аналоговый вход (нестандартный элемент управления)
inline constexpr bool ENABLE_SLIDER2 = false;		// Доп. аналоговый вход (нестандартный элемент управления)

inline constexpr bool ENABLE_RUDDER = false;		// Аналоговая ось для управления поворотом вокруг вертикальной оси (авиасим)
inline constexpr bool ENABLE_THROTTLE = false;		// Аналоговая ось, которая управляет мощностью двигателя (авиасим)
inline constexpr bool ENABLE_ACCELERATOR = false;	// Аналоговая педаль газа (автосим)
inline constexpr bool ENABLE_BRAKE = false;			// Аналоговая педаль тормоза (автосим)
inline constexpr bool ENABLE_STEERING = false;		// Аналоговая ось, которая передает угол поворота руля (автосим)

inline constexpr bool ENABLE_START = true;			// Кнопка Start
inline constexpr bool ENABLE_SELECT = true;			// Кнопка Select
inline constexpr bool ENABLE_MENU = false;			// Кнопка Меню
inline constexpr bool ENABLE_HOME = false;			// Кнопка Домой
inline constexpr bool ENABLE_BACK = false;			// Кнопка Назад
inline constexpr bool ENABLE_VOLUMEINC = false;		// Кнопка Увеличить Громкость
inline constexpr bool ENABLE_VOLUMEDEC = false;		// Кнопка Уменьшить Громкость
inline constexpr bool ENABLE_VOLUMEMUTE = false;	// Кнопка Выключить Звук

//------------------------------------------------------------------

// === Общие настройки ===
inline constexpr TypeMS REPORT_INTERVAL_MS = 10;						// Частота считывания кнопок и отправки отчета в МС

inline constexpr TypeMS COMBINATION_HOLD_MS = 3ul * 1000;				// Активация комбинации кнопок удержанием 3 секунды
inline constexpr TypeMS TURBO_MS = 33;									// Время смены Турбо-фазы в МС (общее для всех режимов)
inline constexpr TypeMS SLOW_MS = 100;									// Время смены фазы нажатия/отпускания кнопки START в МС (в Slow режиме)

inline constexpr uint16_t DISCONNECTED_BLINK_MS = 15ul * 100;			// Период мигания светодиода сопряжения при отсутствии BT-соединения 1.5 секунды
inline constexpr TypeMS CONNECTION_INTERVAL_MS = 1000;					// Интервал проверки наличия BT-сопряжения в МС
inline constexpr TypeMS INACTIVITY_CONNECTED_MS = 5ul * 1000 * 60;		// Автоотключение при бездействии 5 мин
inline constexpr TypeMS INACTIVITY_DISCONNECTED_MS = 2ul * 1000 * 60;	// Автоотключение при отсутствии подключения 2 мин

// Не допускаем опрос кнопок реже чем турбо пульсация
static_assert(REPORT_INTERVAL_MS <= TURBO_MS,
				"Button polling occurs less frequently than expected for turbo button pulsing. "
				"Turbo pulsing may be missed or delayed. If this is normal for your code, comment out this warning.");

//------------------------------------------------------------------

// === Все что связано с измерением заряда аккумулятора ===
inline constexpr uint8_t BATTERY_READ_COUNT = 64;						// Количество считываний напряжения аккумулятора для точности
inline constexpr uint16_t VOLTAGE_ACCURACY_LEVEL = 80;					// Порог погрешности измерений в мВ
inline constexpr uint16_t BATTERY_WARNING_LEVEL = 3300;					// При этом уровне заряда батареи моргает LED для предупреждения о низком заряде
inline constexpr uint16_t BATTERY_CRITICAL_LEVEL = 3100;				// При этом уровне заряда быстро моргает LED и геймпад отключается
//inline constexpr uint8_t BATTERY_WARNING_PERCENT = 20;					// При этом проценте заряда батареи моргает LED для предупреждения о низком заряде
//inline constexpr uint8_t BATTERY_CRITICAL_PERCENT = 5;					// При этом проценте заряда быстро моргает LED и геймпад отключается
inline constexpr TypeMS BATTERY_INTERVAL_MS = 30ul * 1000;				// Интервал проверки заряда аккумулятора в МС (оптимально 30-60 секунд)

inline constexpr float BAT_PERCENT_STEP = 2.0f;

// Таблица напряжений (мВ) от 100% (4.20 В) до 0% (3.10 В) с шагом BAT_PERCENT_STEP в %
// Составлена при помощи ИИ на основании усредненных данных по кривым разряда нескольких популярных Li-Ion аккумуляторов
// 2% шаг, cutoff 3100 мВ
inline constexpr std::array<uint16_t, 51> BAT_VOLTAGE_MAP =
{
    4200, 4160, 4130, 4100, 4070, 4040, 4010, 3980,
    3950, 3920, 3890, 3860, 3830, 3800, 3780, 3765,
    3750, 3735, 3720, 3710, 3700, 3690, 3680, 3670,
    3660, 3650, 3640, 3630, 3620, 3610, 3600, 3585,
    3570, 3550, 3530, 3510, 3490, 3465, 3440, 3410,
    3375, 3340, 3310, 3280, 3250, 3225, 3200, 3175,
    3150, 3125, 3100
};

//------------------------------------------------------------------

// === Все доступные варианты кнопок ===
enum class GamepadButton : uint8_t
{
	A,
	B,
	C,
	X,
	Y,
	Z,
	L1,
	R1,
	L3,
	R3,
	SELECT,
	START,
	MENU,
	HOME,
	BACK,
	VOLUME_INC,
	VOLUME_DEC,
	VOLUME_MUTE,
	DPAD_BTN_UP,
	DPAD_BTN_DOWN,
	DPAD_BTN_LEFT,
	DPAD_BTN_RIGHT,
	BUTTONS_COUNT
};

inline constexpr uint8_t BUTTONS_VARIANTS = static_cast<uint8_t>(GamepadButton::BUTTONS_COUNT);

//------------------------------------------------------------------

// === Комбинации кнопок для системных действий ===

// Выключение (переход в глубокий сон)
inline constexpr std::array<GamepadButton, 3> POWEROFF_COMBO =
{
	GamepadButton::DPAD_BTN_DOWN,
	GamepadButton::A,
	GamepadButton::B
};

// Смена режима (Standard/Turbo/Slow)
inline constexpr std::array<GamepadButton, 3> CHMODE_COMBO =
{
	GamepadButton::DPAD_BTN_UP,
	GamepadButton::A,
	GamepadButton::B
};

//------------------------------------------------------------------

// === Конфигурация пинов геймпада ===
inline constexpr uint8_t PWRLED_PIN = 20;	// Сигнал о низком заряде АКБ
inline constexpr uint8_t BTLED_PIN = 21;	// Состояние сопряжения (мигает - режим видимости, горит - соединен)

inline constexpr uint8_t POWER_PIN = 4;     //Аналоговый пин считывающий уровень напряжения


inline uint8_t getButtonPin(GamepadButton btn)
{
	switch(btn)
	{
		case GamepadButton::START:				return 5;
		case GamepadButton::SELECT:				return 6;
		
		case GamepadButton::A:					return 9;
		case GamepadButton::B:					return 10;

		case GamepadButton::DPAD_BTN_UP:		return 2;
		case GamepadButton::DPAD_BTN_RIGHT:		return 3;
		case GamepadButton::DPAD_BTN_DOWN:		return 7;
		case GamepadButton::DPAD_BTN_LEFT:		return 8;
		
		default: return UINT8_MAX; // Новая/неподключённая кнопка
	}
}