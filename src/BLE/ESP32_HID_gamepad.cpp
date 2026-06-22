
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

#include "ESP32_HID_gamepad.hpp"
#include "IDeviceAdapter.hpp"
#include "../gamepadConfig.hpp"

#include <memory>
#include <BleCompositeHID.h>
#include <BLEHostConfiguration.h>
#include <GamepadConfiguration.h>
#include <XboxGamepadConfiguration.h>

//------------------------------------------------------------------

ESP32_HID_gamepad::ESP32_HID_gamepad(const char* deviceName, const char* deviceManufacturer, uint8_t batteryLevel)
	: currButtonMode(ESP32_HID_gamepad::ButtonMode::Standard)
	, currGamepadType(ESP32_HID_gamepad::GamepadType::Generic)
	, compositeHID(deviceName, deviceManufacturer, batteryLevel)
	, ptrGamepadDevice()
	, currBtns()
	, prevBtns()
	, turboBtns()
	, powerOffCombo()
	, changeButtonModeCombo()
{
}

bool ESP32_HID_gamepad::begin(GamepadType mode /*= GamepadType::Generic*/)
{
	if (ptrGamepadDevice)
		return false;
	
	
	currGamepadType = mode;
	switch (currGamepadType)
	{
		case GamepadType::Generic:
		{
			GamepadDeviceAdapter* device = new GamepadDeviceAdapter();
			ptrGamepadDevice.reset(device);
			break;
		}
		
		case GamepadType::XBoxOneS:
		case GamepadType::XBoxSeriesX:
		{
			IDeviceAdapter* device = nullptr;

			if (currGamepadType == GamepadType::XBoxOneS)		device = new XboxOneSDeviceAdapter();
			else												device = new XboxSeriesXDeviceAdapter();

			if (!device)
				return false;

			ptrGamepadDevice.reset(device);

			// Левый и правый стики
			ptrGamepadDevice->setLeftThumb(0, 0);
			ptrGamepadDevice->setRightThumb(0, 0);

			// Левый и правый триггер
			ptrGamepadDevice->setLeftTrigger(0);
			ptrGamepadDevice->setRightTrigger(0);
			
			break;
		}
		
		default:
			return false;
	}

	if (!ptrGamepadDevice)
		return false;
	
	ptrGamepadDevice->attachDeviceToParent(compositeHID);

	BLEHostConfiguration bleHostConfig = ptrGamepadDevice->getIdealHostConfiguration();
	compositeHID.begin(bleHostConfig);

	return true;
}

void ESP32_HID_gamepad::end()
{
	if (!ptrGamepadDevice)
		return;
	
	// Отправляем  пустой отчет (все кнопки и оси отпущены)
	sendAllEmptyReport();
	
	compositeHID.end();
	ptrGamepadDevice.reset(/*nullptr*/);

	// TODO NimBLE kill
}

//------------------------------------------------------------------

// Установка состояний кнопок и осей
void ESP32_HID_gamepad::setAxisState(int16_t leftX, int16_t leftY, int16_t rightX, int16_t rightY)
{
	currBtns.x1 = leftX;
	currBtns.y1 = leftY;
	currBtns.x2 = rightX;
	currBtns.y2 = rightY;
}

void ESP32_HID_gamepad::setTriggerState(int16_t l2, int16_t r2)
{
	currBtns.l2 = l2;
	currBtns.r2 = r2;
}

void ESP32_HID_gamepad::setButtonState(GamepadButton btn, bool isPressed)
{
	setButtonInReport(currBtns, btn, isPressed);
}


// Отправка кнопок и осей
void ESP32_HID_gamepad::sendReport()
{
	if (!ptrGamepadDevice /*|| !compositeHID.isConnected()*/)
		return;
	
	ButtonsCombination tmp = currBtns;	// Состояние кнопок для модификации
	bool shouldSend = false;			// Требуется ли отправка (нажали/отпустили кнопку или настала фаза Турбо)

	// ================================================================================================

	// Общая Турбо-пульсация для режимов Standard/Slow (отдельные кнопки помеченные как Турбо) и Turbo
	// Применение маски к комбинации текущих кнопок tmp и возврат true при смене Турбо-фазы
	auto applyTurboPhase = [&tmp](MaskType mask) -> bool
	{
		static bool prevTurboState = false;		// Последнее состояние Турбо-фазы (кнопки нажаты/отпущены)
		static TypeMS prevTurboTime = 0;		// Предыдущее время смены состояния кнопок в Турбо-фазе
		
		const TypeMS now = millis();
		const bool currTurboState = (now - prevTurboTime) >= TURBO_MS;
		
		// Прошло TURBO_MS и у нас есть хотя бы какая-то маска 
		if (currTurboState && mask)
		{
			tmp.buttons &= ~mask;
			prevTurboTime = now;
		}

		const bool phaseChanged = (currTurboState != prevTurboState);
		prevTurboState = currTurboState;	// Обновляем всегда, чтобы отслеживать каждый переход фазы
		
		return phaseChanged;
	};

	// ================================================================================================

	// Режим Standard (обычные нажатия/удержания + Турбо-пульсация для заданных пользователем кнопок)
	auto processStandard = [&]() -> bool
	{
		// Применяем турбо-пульсацию к явно заданным отдельным Турбо-кнопкам
		const MaskType turboMask = turboBtns.buttons;
		const bool phaseChanged = applyTurboPhase(turboMask);
		
		// Одно из условий отправки - сменились физически нажатые/отпущенные кнопки
		bool send = (currBtns != prevBtns);

		// Второе условие отправки в режиме Standard - физически нажата одна из Турбо-кнопок,  
		// целенаправленно заданных пользователем класса, и прошло заданное время для смены Турбо-фазы
		const bool turboPressed = (currBtns.buttons & turboMask);
		if (phaseChanged && turboPressed)
			send = true;
		
		return send;
	};

	// ================================================================================================

	// Режим Turbo (все цифровые кнопки действий при нажатии и удержании многократно срабатывают:
	//				нажимаются и отпускаются программно, пока физически нажата кнопка)
	
	// Вспомогательная лямбда-функция с fold expression (создается и используется только на этапе компиляции)
	auto combineMasks = [](auto... btns) constexpr -> MaskType
	{
		return (... | getBitOffset(btns));
	};

	// Турбо-маска для всех цифровых кнопок действия
	constexpr MaskType ALL_TURBO_BUTTONS_MASK = combineMasks
	(
		GamepadButton::A,  GamepadButton::B,  GamepadButton::C,
		GamepadButton::X,  GamepadButton::Y,  GamepadButton::Z,
		GamepadButton::L1, GamepadButton::R1,
		GamepadButton::L3, GamepadButton::R3
	);

	auto processTurbo = [&]() -> bool
	{
		const MaskType turboMask = ALL_TURBO_BUTTONS_MASK;
		const bool phaseChanged = applyTurboPhase(turboMask);
		
		// Одно из условий отправки - сменились физически нажатые/отпущенные кнопки
		bool send = (currBtns != prevBtns);

		// Второе условие отправки для режима Turbo - физически нажата одна из кнопок действия
		// и прошло заданное время для смены Турбо-фазы
		const bool turboPressed = (currBtns.buttons & turboMask);
		if (phaseChanged && turboPressed)
			send = true;
		
		return send;
	};

	// ================================================================================================

	constexpr MaskType START_MASK = getBitOffset(GamepadButton::START);	// Маска для вкнопки START (в режиме Slow)

	// Режим Slow (мигание START + турбо-пульсация для пользовательских кнопок)
	auto processSlow = [&]() -> bool
	{
		// Применяем турбо-пульсацию к явно заданным отдельным Турбо-кнопкам
		// Также убираем START из турбо-маски, чтобы не сбросить его
		// (обычно его там нет, но защита на всякий случай)
		const MaskType turboMask = turboBtns.buttons & ~START_MASK;
		const bool turboPhaseChanged = applyTurboPhase(turboMask);

		// Одно из условий отправки - сменились физически нажатые/отпущенные кнопки
		bool send = (currBtns != prevBtns);

		// Второе условие отправки в режиме Slow - физически нажата одна из Турбо-кнопок,  
		// целенаправленно заданных пользователем класса, и прошло заданное время для смены Турбо-фазы
		const bool turboPressed = (currBtns.buttons & turboMask);
		if (turboPhaseChanged && turboPressed)
			send = true;
		
		// ========================== Логика пульсации START ==========================
		static bool prevStartState = false;		// Предыдущее состояние кнопки START (нажата/отпущена)
		static TypeMS prevStartTime = 0;		// Предыдущее время смены состояния кнопки START

		const TypeMS now = millis();
		const bool currStartState = (now - prevStartTime) >= SLOW_MS;

		// Третье условие отправки в режиме Slow - сменилась фаза кнопки START
		if (currStartState != prevStartState)
		{
			//				 сброс состояния START		установка состояния START в зависимости от фазы
			tmp.buttons = (tmp.buttons & ~START_MASK) | (currStartState ? START_MASK : 0);
			send = true;
			
			prevStartState = currStartState;
			prevStartTime = now;
		}
		// ========================== Логика пульсации START ==========================
			
		return send;
	};

	// ================================================================================================

	// Корректное поведение в зависимости от текущего режима
	switch (currButtonMode)
	{
		case ButtonMode::Standard: shouldSend = processStandard(); break;
		case ButtonMode::Turbo:    shouldSend = processTurbo();    break;
		case ButtonMode::Slow:     shouldSend = processSlow();     break;
	}

	// Отправка, если требуется
	if (shouldSend)
	{
		sendGamepadReport(tmp);
		prevBtns = currBtns;
	}
}


void ESP32_HID_gamepad::sendAllEmptyReport()
{
	// Обнуляем текущее и предыдущее состояние нажатых кнопок
	currBtns = prevBtns = ButtonsCombination();
	
	if (!ptrGamepadDevice || !compositeHID.isConnected())
		return;

	sendGamepadReport(currBtns);
}

//------------------------------------------------------------------

void ESP32_HID_gamepad::setButtonMode(ButtonMode mode)
{
	if (currButtonMode != mode)
		currButtonMode = mode;
}

bool ESP32_HID_gamepad::isButtonPressed(GamepadButton btn) const
{
	return isButtonInReport(currBtns, btn);
}

//------------------------------------------------------------------

void ESP32_HID_gamepad::sendGamepadReport(const ButtonsCombination& bc)
{
	if (!ptrGamepadDevice)
		return;
	
	// Кнопки и DPad
	for (uint8_t i = 0; i < BUTTONS_VARIANTS; i++)
	{
		GamepadButton btn = static_cast<GamepadButton>(i);
		bool state = isButtonInReport(bc, btn);

		ptrGamepadDevice->setButtonState(btn, state);
	}

	// Левый и правый стики
	if (ENABLE_LTHUMB)	ptrGamepadDevice->setLeftThumb(bc.x1, bc.y1);
	if (ENABLE_RTHUMB)	ptrGamepadDevice->setRightThumb(bc.x2, bc.y2);

	// Левый и правый триггер
	if (ENABLE_L2)		ptrGamepadDevice->setLeftTrigger(bc.l2);
	if (ENABLE_R2)		ptrGamepadDevice->setRightTrigger(bc.r2);

	
	ptrGamepadDevice->sendGamepadReport();
}

//------------------------------------------------------------------

bool ESP32_HID_gamepad::setTurboButton(GamepadButton btn, bool isTurbo)
{
	switch (btn)
	{
		case GamepadButton::A:
		case GamepadButton::B:
		case GamepadButton::C:
		case GamepadButton::X:
		case GamepadButton::Y:
		case GamepadButton::Z:
		case GamepadButton::L1:
		case GamepadButton::R1:
		case GamepadButton::L3:
		case GamepadButton::R3:
			setButtonInReport(turboBtns, btn, isTurbo);
			return true;
	}

	return false;
}

bool ESP32_HID_gamepad::isTurboButton(GamepadButton btn) const
{
	return isButtonInReport(turboBtns, btn);
}

//------------------------------------------------------------------

ESP32_HID_gamepad::ButtonsCombination ESP32_HID_gamepad::createCombo(const GamepadButton* combo, uint8_t count)
{
	ESP32_HID_gamepad::ButtonsCombination testCombo;
	
	if (!combo || !count)
		return testCombo;
	
	for (uint8_t i = 0; i < count; i++)
		setButtonInReport(testCombo, combo[i], true);
	
	return testCombo;
}

// Установка комбинаций
bool ESP32_HID_gamepad::setPowerOffCombination(const GamepadButton* combo, uint8_t count)
{
	auto testCombo = createCombo(combo, count);
	if (testCombo.isEmpty() || testCombo == changeButtonModeCombo)
		return false;

	powerOffCombo = testCombo;
	return true;
}

bool ESP32_HID_gamepad::setChangeButtonModeCombination(const GamepadButton* combo, uint8_t count)
{
	auto testCombo = createCombo(combo, count);
	if (testCombo.isEmpty() || testCombo == powerOffCombo)
		return false;

	changeButtonModeCombo = testCombo;
	return true;
}

//------------------------------------------------------------------

// Проверка комбинаций
bool ESP32_HID_gamepad::isPowerOffCombination(const GamepadButton* combo, uint8_t count)
{
	auto testCombo = createCombo(combo, count);
	return !testCombo.isEmpty() && testCombo == powerOffCombo;
}

bool ESP32_HID_gamepad::isPowerOffCombination() const
{
	return !powerOffCombo.isEmpty()  &&  powerOffCombo == currBtns;
}

bool ESP32_HID_gamepad::isChangeButtonModeCombination(const GamepadButton* combo, uint8_t count)
{
	auto testCombo = createCombo(combo, count);
	return !testCombo.isEmpty()  &&  testCombo == changeButtonModeCombo;
}

bool ESP32_HID_gamepad::isChangeButtonModeCombination() const
{
	return !changeButtonModeCombo.isEmpty()  &&  changeButtonModeCombo == currBtns;
}

//------------------------------------------------------------------

// Хелпер
void ESP32_HID_gamepad::setButtonInReport(ButtonsCombination& bc, GamepadButton btn, bool state)
{
	const MaskType mask = getBitOffset(btn);
	if (mask == static_cast<MaskType>(GamepadButton::BUTTONS_COUNT))
		return;

	if (state)	bc.buttons |= mask;
	else		bc.buttons &= ~mask;
}

bool ESP32_HID_gamepad::isButtonInReport(const ButtonsCombination& bc, GamepadButton btn) const
{
	const MaskType mask = getBitOffset(btn);
	if (mask == static_cast<MaskType>(GamepadButton::BUTTONS_COUNT))
		return false;

	return (bc.buttons & mask);
}
