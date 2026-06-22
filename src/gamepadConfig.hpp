
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

#define USE_MAX1704X_FUEL_GAGE false	// false - если заряд аккумулятора определяется на основании напряжения полученного с делителя на резисторах
										// true - если для измерения заряда аккумулятора используется микросхема серии MAX1704X


#if !USE_MAX1704X_FUEL_GAGE
	#define USE_VOLTAGE_MEASURE_DEBUG false		// Используется (true) или нет (false) Serial-отладка измерения напряжения
#endif


// === ВЫБОР ТИПА ГЕЙМПАДА ===
#define ONLY_DIGITAL_GAMEPAD			// Только цифровые кнопки без аналоговых стиков/триггеров
										// 2ух кнопочные: NES, Sega Master System, TurboGrafx, atari 7800, Amstrad GX4000, ...
										// 4ех кнопочные: SNES, Neo Geo, Neo Geo CD, Vectrex, Philips CD-i, Amiga CD32, ...
										// 3ех/6ти кнопочные: Sega Saturn, Sega Genesis/MD2, Panasonic 3DO, ~Atari Jaguar, ~NEC PC-FX

//#define ANALOGUE_GAMEPAD				// Наличие аналоговых стиков и триггеров
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
inline constexpr std::string_view SERIAL_NUMBER = "001";				// Серийный номер устройства (увеличивать для каждого след. устройства)
inline constexpr std::string_view SOFTWARE_REVISION = "0.5.0";			// Версия прошивки
inline constexpr std::string_view HARDWARE_REVISION = "1.2";			// Ревизия платы/железа/конфигурации
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
inline constexpr TypeMS WDT_TIMEOUT_MS = 10ul * 1000;					// Таймаут для следящего таймера (защита от зависаний) в МС
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
inline constexpr uint8_t  BATTERY_WARNING_PERCENT = 10;			// При этом проценте заряда батареи моргает LED для предупреждения о низком заряде
inline constexpr uint8_t  BATTERY_CRITICAL_PERCENT = 2;			// При этом проценте заряда быстро моргает LED и геймпад отключается
inline constexpr TypeMS   BATTERY_INTERVAL_MS = 60ul * 1000;	// Интервал проверки заряда аккумулятора в МС (оптимально 30-60 секунд)

inline constexpr uint16_t MID_CHARGE_DETECT_MV = 3700;			// Порог напряжения для определения возможной зарядки, в мВ
inline constexpr uint8_t  CHARGE_RISE_DETECT_MV = 150;			// Минимальный скачок для детекции начала зарядки батареи, в мВ
inline constexpr uint8_t  CHARGE_FALL_DETECT_MV = 50;			// Минимальная прсоадка для детекции окончания зарядки батареи, в мВ
inline constexpr uint8_t  PERCENT_JUMP_PER_CYCLE = 3;			// Максимальный прирост процента за один цикл пробуждения при зарядке (сглаживание)

#if !USE_MAX1704X_FUEL_GAGE
	// Измерено: при реальных 5070 мВ на источнике питания, analogReadMilliVolts() выдаёт 2543 мВ
	// Коэффициент коррекции = 5070 / 2543
	// Показания при делителе Rверх = 100 кОм и Rниз = 100 кОм + Cниз = 0.1 мкФ
	// Rверх - от Vbat+ к пину измерения, Rниз - от GND к пину измерения
	inline constexpr uint16_t VBAT_CALIBRATION_MV = 5070;		// Реальное напряжение на источнике питания в мВ
	inline constexpr uint16_t RAW_CALIBRATION_MV = 2543;		// Показание analogReadMilliVolts на пине в мВ

	inline constexpr uint8_t BATTERY_READ_COUNT = 32;			// Количество считываний напряжения аккумулятора для точности
	inline constexpr uint8_t BATTERY_THRESHOLD_MV = 50;			// Считаем что при измерении на делителе имеется такая погрешность в мВ

	// Таблица напряжений (мВ) от 0% (3.20 В) до 100% (4.20 В) с шагом 1% (индекс == процент)
	// Copyright (c) 2021 Danilo Pinotti
	// Лицензия: MIT (https://github.com/danilopinotti/Battery18650Stats/blob/main/LICENSE)
	// Оригинальная библиотека не используется, т.к. в ней применяется тяжеловесный double
	inline constexpr std::array<uint16_t, 101> BAT_VOLTAGE_MAP =
	{
		// 0 	1	  2		3	  4		5	  6		7	  8		9
		3200, 3250, 3300, 3350, 3400, 3450, 3500, 3550, 3600, 3650,		//  0 - 9
		3700, 3703, 3706, 3710, 3713, 3716, 3719, 3723, 3726, 3729,		// 10 - 19
		3732, 3735, 3739, 3742, 3745, 3748, 3752, 3755, 3758, 3761,		// 20 - 29
		3765, 3768, 3771, 3774, 3777, 3781, 3784, 3787, 3790, 3794,		// 30 - 39
		3797, 3800, 3805, 3811, 3816, 3821, 3826, 3832, 3837, 3842,		// 40 - 49
		3847, 3853, 3858, 3863, 3868, 3874, 3879, 3884, 3889, 3895,		// 50 - 59
		3900, 3906, 3911, 3917, 3922, 3928, 3933, 3939, 3944, 3950,		// 60 - 69
		3956, 3961, 3967, 3972, 3978, 3983, 3989, 3994, 4000, 4008,		// 70 - 79
		4015, 4023, 4031, 4038, 4046, 4054, 4062, 4069, 4077, 4085,		// 80 - 89
		4092, 4100, 4111, 4122, 4133, 4144, 4156, 4167, 4178, 4189,		// 90 - 99
		4200
	};
#endif

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

#if USE_MAX1704X_FUEL_GAGE
	inline constexpr uint8_t SDA_PIN = 0;	// Пин подключающийся к SDA пину MAX1704x
	inline constexpr uint8_t SCL_PIN = 1;	// Пин подключающийся к SCL пину MAX1704x
#else
	inline constexpr uint8_t POWER_PIN = 4;	//Аналоговый пин считывающий уровень напряжения
#endif


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
		
		default: return UINT8_MAX;		// Новая/неподключённая кнопка
	}
}