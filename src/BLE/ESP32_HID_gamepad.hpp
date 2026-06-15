
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
#include <memory>

#include <BleCompositeHID.h>

#include "IDeviceAdapter.hpp"
#include "../gamepadConfig.hpp"

//=====================================================================================================================================

class ESP32_HID_gamepad
{
	struct ButtonsCombination
	{
		uint32_t buttons{0};
		int16_t l2{0}, r2{0};				// Левый и правый триггер
		int16_t x1{0}, y1{0}, x2{0}, y2{0};	// Левый и правый стики
		
		bool isEmpty() const
		{
			return buttons == 0 
					&& l2 == 0 && r2 == 0
					&& x1 == 0 && y1 == 0 
					&& x2 == 0 && y2 == 0;
		}
		
		bool operator==(const ButtonsCombination& other) const
		{
			return buttons == other.buttons
					&& l2 == other.l2 && r2 == other.r2
					&& x1 == other.x1 && y1 == other.y1
					&& x2 == other.x2 && y2 == other.y2;
		}
		
		bool operator!=(const ButtonsCombination& other) const
		{
			return !(*this == other);
		}
	};

	// Количество кнопок включая D-Pad не может превышать вместимость переменной, хранящей флаги их состояний,
	// по смещению относительно значения enum GamepadButton
	static_assert(BUTTONS_VARIANTS <= sizeof(ButtonsCombination::buttons) * 8,
					"Not enough bits in ButtonsCombination::buttons for all buttons"
					" (Button::ButtonCount > sizeof(ButtonsCombination::buttons) * 8)");


	using MaskType = decltype(ButtonsCombination::buttons);

public:
	enum ButtonMode : uint8_t
	{
		Standard,
		Turbo,		// Множественное нажатие цифровых кнопок действия A/B/C/X/Z/Y/L1/R1/L3/R3
		Slow,		// Быстрое нажатие-отпускание кнопки START для эмуляции слоумо
		ButtonModeCount
	};

	enum GamepadType : uint8_t
	{
		Generic,			// D-Input
		XBoxOneS,			// X-Input
		XBoxSeriesX,		// X-Input
		//DualSense,		// PlayStation controller (with touchpad & accelorometer)
		GamepadTypeCount
	};


public:
	ESP32_HID_gamepad();

	bool begin(GamepadType mode = GamepadType::Generic);
	void end();

	// Установка состояний осей и кнопок ДО ОТПРАВКИ
	void setAxisState(int16_t leftX, int16_t leftY, int16_t rightX, int16_t rightY);	// Левый и правый стик  (-32768 .. 32767)
	void setTriggerState(int16_t l2, int16_t r2);										// Триггеры L2/R2 для Generic (-32768 .. 32767), для XBox (0 .. 1023)
	void setButtonState(GamepadButton btn, bool isPressed);

	// Отправка HID-репорта
	void sendReport();
	
	// Отправка всех состояний как отпущенных кнопок/стиков/триггеров
	void sendAllEmptyReport();
	
	bool isNewCombination() const { return currBtns != prevBtns; };
	bool isButtonPressed(GamepadButton btn) const;
	bool isAnyButtonPressed() const { return !currBtns.isEmpty(); };

	// Включить/выключить Tubro-функционал для отдельной кнопки ДЕЙСТВИЯ (только цифровые кнопки, без аналоговых осей)
	bool setTurboButton(GamepadButton btn, bool isTurbo);
	bool isTurboButton(GamepadButton btn) const;
	bool isTurboSetForAnyButton() const { return !turboBtns.isEmpty(); }

	// Смена режима работы геймпада: Standard, Turbo, Slow
	void setButtonMode(ButtonMode mode);
	ButtonMode getButtonMode() { return currButtonMode; }
	bool setChangeButtonModeCombination(const GamepadButton* combo, uint8_t count);    //Только цифровые кнопки, без аналоговых осей
	bool isChangeButtonModeCombination(const GamepadButton* combo, uint8_t count);
	bool isChangeButtonModeCombination() const;
	

	bool setPowerOffCombination(const GamepadButton* combo, uint8_t count);            //Только цифровые кнопки, без аналоговых осей
	bool isPowerOffCombination(const GamepadButton* combo, uint8_t count);
	bool isPowerOffCombination() const;
	
	// Установленно ли соединение (сопряжение)
	bool isConnected()	{ return ptrGamepadDevice && compositeHID.isConnected(); }

	// Установить уровень заряда батареи, от 0 до 100%
	void setBatteryLevel(uint8_t level)
	{
		level = (level > 100) ? 100 : level;
		compositeHID.setBatteryLevel(level);
	}

private:
	// Отправка HID-репорта через интерфейс конкретного наследника BaseCompositeDevice
	void sendGamepadReport(const ButtonsCombination& bc);

	// Установка состояний кнопок в комбинации (установка бита кнопки в нужном байте - ON/OFF)
	void setButtonInReport(ButtonsCombination& bc, GamepadButton btn, bool state);
	
	// Включен ли бит кнопки в комбинации
	bool isButtonInReport(const ButtonsCombination& bc, GamepadButton btn) const;
	
	// Создание комбинации из указанного массива
	ButtonsCombination createCombo(const GamepadButton* combo, uint8_t count);


	static constexpr MaskType getBitOffset(GamepadButton btn)
	{
		const uint8_t btnOffset = static_cast<uint8_t>(btn);
		if (btnOffset >= (sizeof(ButtonsCombination::buttons) * 8))
			return static_cast<MaskType>(GamepadButton::BUTTONS_COUNT);

		return static_cast<MaskType>(1) << btnOffset;
	}

private:
	ButtonMode currButtonMode;
	GamepadType currGamepadType;

	BleCompositeHID compositeHID;
	std::unique_ptr<IDeviceAdapter> ptrGamepadDevice;


	ButtonsCombination currBtns;
	ButtonsCombination prevBtns;
	
	ButtonsCombination turboBtns; // Кнопки, на которых всегда включен режим турбо
	
	ButtonsCombination powerOffCombo;
	ButtonsCombination changeButtonModeCombo;
};