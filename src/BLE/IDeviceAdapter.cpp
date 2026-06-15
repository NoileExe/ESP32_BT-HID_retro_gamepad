
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

#include "IDeviceAdapter.hpp"
#include "../gamepadConfig.hpp"

#include <stdint.h>
#include <memory>

#include <BleCompositeHID.h>
#include <BLEHostConfiguration.h>
#include <GamepadDevice.h>
#include <GamepadConfiguration.h>
#include <XboxGamepadDevice.h>
#include <XboxGamepadConfiguration.h>
//#include <DualsenseGamepadDevice.h>
//#include <DualsenseGamepadConfiguration.h>

//------------------------------------------------------------------

BLEHostConfiguration IDeviceAdapter::getIdealHostConfiguration() const
{
	BLEHostConfiguration bleHostConfig;
	
	bleHostConfig.setModelNumber(MODEL_NUMBER.data());
	bleHostConfig.setSoftwareRevision(SOFTWARE_REVISION.data());
	bleHostConfig.setFirmwareRevision(FIRMWARE_REVISION.data());
	bleHostConfig.setHardwareRevision(HARDWARE_REVISION.data());

	/*bleHostConfig.setMinConnectionInterval(6);	// 1.25 * 6 = 7.5 мс | более экономично поставить (12)
	bleHostConfig.setMaxConnectionInterval(12);	// 1.25 * 12 = 15 мс | более экономично поставить (24)
	bleHostConfig.setSlaveLatency(0);			// Не позволяем пропускать циклы общения с хостом
	bleHostConfig.setSupervisionTimeout(500);	// Через 500 * 10 мс = 5 сек после разрыва сопряжения, оно считается завершенным
	bleHostConfig.setQueuedSending(false);		// Не накапливаем сообщения, отправляем сразу*/

	getSpecificAdapterConfiguration(bleHostConfig);

	return bleHostConfig;
}

//------------------------------------------------------------------

// Адаптер для GamepadDevice
GamepadDeviceAdapter::GamepadDeviceAdapter()
	: directionFlags(0)
	, ptrGamepadDevice()
{
	GamepadConfiguration bleGamepadConfig;
	bleGamepadConfig.setAutoReport(false);
	bleGamepadConfig.setControllerType(CONTROLLER_TYPE_GAMEPAD);
	bleGamepadConfig.setButtonCount(ALL_BUTTONS_COUNT);
	bleGamepadConfig.setWhichSpecialButtons(ENABLE_START, ENABLE_SELECT,
											ENABLE_MENU, ENABLE_HOME, ENABLE_BACK,
											ENABLE_VOLUMEINC, ENABLE_VOLUMEDEC, ENABLE_VOLUMEMUTE);

	bleGamepadConfig.setHatSwitchCount(HAT_SWITCHES_COUNT);
	bleGamepadConfig.setWhichAxes(ENABLE_LTHUMB, ENABLE_LTHUMB, ENABLE_RTHUMB,
									ENABLE_L2, ENABLE_R2, ENABLE_RTHUMB,
									ENABLE_SLIDER1, ENABLE_SLIDER2);
			
	bleGamepadConfig.setWhichSimulationControls(ENABLE_RUDDER, ENABLE_THROTTLE,
												ENABLE_ACCELERATOR, ENABLE_BRAKE, ENABLE_STEERING);

	// Some non-Windows operating systems and web based gamepad testers don't like min axis set below 0, so 0 is set by default
	bleGamepadConfig.setAxesMin(0x8001); // -32767 --> int16_t - 16 bit signed integer - Can be in decimal or hexadecimal
	bleGamepadConfig.setAxesMax(0x7FFF); // 32767 --> int16_t - 16 bit signed integer - Can be in decimal or hexadecimal 

	GamepadDevice* device = new GamepadDevice(bleGamepadConfig);
	ptrGamepadDevice.reset(device);
}

void GamepadDeviceAdapter::attachDeviceToParent(BleCompositeHID& parent)
{
	if (ptrGamepadDevice)
		parent.addDevice(ptrGamepadDevice.get());
}

void GamepadDeviceAdapter::getSpecificAdapterConfiguration(BLEHostConfiguration& bleHostConfig) const
{
	bleHostConfig.setHidType(HID_GAMEPAD);
	//bleHostConfig.setVid(0xe502);
	//bleHostConfig.setPid(0xabcd);
			
	bleHostConfig.setSerialNumber(SERIAL_NUMBER.data());
}

void GamepadDeviceAdapter::resetButtons()
{
	if (!ptrGamepadDevice)
		return;

	directionFlags = 0;
	ptrGamepadDevice->resetButtons();
}

void GamepadDeviceAdapter::setButtonState(GamepadButton btn, bool state)
{
	if (!ptrGamepadDevice)
		return;

	switch (btn)
	{
		case GamepadButton::A: case GamepadButton::B: case GamepadButton::C:
		case GamepadButton::X: case GamepadButton::Y: case GamepadButton::Z:
		case GamepadButton::L1: case GamepadButton::R1:
		case GamepadButton::L3: case GamepadButton::R3:
		{			
			uint8_t button_code = getNormalButtonCode(btn);
			
			if (state)		ptrGamepadDevice->press(button_code);
			else			ptrGamepadDevice->release(button_code);

			break;
		}

		case GamepadButton::SELECT: case GamepadButton::START:
		case GamepadButton::MENU: case GamepadButton::HOME: case GamepadButton::BACK:
		case GamepadButton::VOLUME_INC: case GamepadButton::VOLUME_DEC: case GamepadButton::VOLUME_MUTE:
		{
			uint8_t button_code = getSpecialButtonCode(btn);

			if (state)		ptrGamepadDevice->pressSpecialButton(button_code);
			else			ptrGamepadDevice->releaseSpecialButton(button_code);

			break;
		}

		case GamepadButton::DPAD_BTN_UP:   case GamepadButton::DPAD_BTN_DOWN:
		case GamepadButton::DPAD_BTN_LEFT: case GamepadButton::DPAD_BTN_RIGHT:
		{
			uint8_t flag = getDirectionFlag(btn);

			if (state)	directionFlags |= flag;
			else		directionFlags &= ~flag;

			break;
		}
	}
}

uint8_t GamepadDeviceAdapter::getNormalButtonCode(GamepadButton btn) const
{
	switch (btn)
	{
		case GamepadButton::A:		return static_cast<uint8_t>(BUTTON_1);
		case GamepadButton::B:		return static_cast<uint8_t>(BUTTON_2);
		case GamepadButton::C:		return static_cast<uint8_t>(BUTTON_3);
		case GamepadButton::X:		return static_cast<uint8_t>(BUTTON_4);
		case GamepadButton::Y:		return static_cast<uint8_t>(BUTTON_5);
		case GamepadButton::Z:		return static_cast<uint8_t>(BUTTON_6);
		case GamepadButton::L1:		return static_cast<uint8_t>(BUTTON_7);
		case GamepadButton::R1:		return static_cast<uint8_t>(BUTTON_8);
		case GamepadButton::L3:		return static_cast<uint8_t>(BUTTON_14);		// В Windows мо;tт определяться как кнопка DPad. На Android все хорошо
		case GamepadButton::R3:		return static_cast<uint8_t>(BUTTON_15);		// В Windows мо;tт определяться как кнопка DPad. На Android все хорошо
	}

	return static_cast<uint8_t>(-1);
}

uint8_t GamepadDeviceAdapter::getSpecialButtonCode(GamepadButton btn) const
{
	switch (btn)
	{
		case GamepadButton::START:			return static_cast<uint8_t>(START_BUTTON);
		case GamepadButton::SELECT:			return static_cast<uint8_t>(SELECT_BUTTON);
		case GamepadButton::MENU:			return static_cast<uint8_t>(MENU_BUTTON);
		case GamepadButton::HOME:			return static_cast<uint8_t>(HOME_BUTTON);
		case GamepadButton::BACK:			return static_cast<uint8_t>(BACK_BUTTON);
		case GamepadButton::VOLUME_INC:		return static_cast<uint8_t>(VOLUME_INC_BUTTON);
		case GamepadButton::VOLUME_DEC:		return static_cast<uint8_t>(VOLUME_DEC_BUTTON);
		case GamepadButton::VOLUME_MUTE:	return static_cast<uint8_t>(VOLUME_MUTE_BUTTON);
	}

	return static_cast<uint8_t>(-1);
}

uint8_t GamepadDeviceAdapter::getDirectionFlag(GamepadButton btn) const
{
	switch (btn)
	{
		case GamepadButton::DPAD_BTN_UP:		return static_cast<uint8_t>(DirectionFlag::D_UP);
		case GamepadButton::DPAD_BTN_DOWN:		return static_cast<uint8_t>(DirectionFlag::D_DOWN);
		case GamepadButton::DPAD_BTN_LEFT:		return static_cast<uint8_t>(DirectionFlag::D_LEFT);
		case GamepadButton::DPAD_BTN_RIGHT:		return static_cast<uint8_t>(DirectionFlag::D_RIGHT);
	}

	return static_cast<uint8_t>(DirectionFlag::D_CENTERED);
}


void GamepadDeviceAdapter::setLeftThumb(int16_t leftX, int16_t leftY)
{
	if (!ptrGamepadDevice)
		return;

	ptrGamepadDevice->setLeftThumb(leftX, leftY);
}

void GamepadDeviceAdapter::setRightThumb(int16_t rightX, int16_t rightY)
{
	if (!ptrGamepadDevice)
		return;

	ptrGamepadDevice->setRightThumb(rightX, rightY);
}

void GamepadDeviceAdapter::setLeftTrigger(int16_t l2)
{
	if (!ptrGamepadDevice)
		return;

	ptrGamepadDevice->setLeftTrigger(l2);
}

void GamepadDeviceAdapter::setRightTrigger(int16_t r2)
{
	if (!ptrGamepadDevice)
		return;

	ptrGamepadDevice->setRightTrigger(r2);
}


void GamepadDeviceAdapter::sendGamepadReport()
{
	if (!ptrGamepadDevice)
		return;
	
	const int8_t dpadState = directionFlagsToValue();
	ptrGamepadDevice->setHat1(dpadState);

	ptrGamepadDevice->sendGamepadReport();
}

int8_t GamepadDeviceAdapter::directionFlagsToValue() const
{
	if(directionFlags == DirectionFlag::D_UP)
		return static_cast<int8_t>(DPAD_UP);
	else if(directionFlags == (DirectionFlag::D_RIGHT | DirectionFlag::D_UP))
		return static_cast<int8_t>(DPAD_UP_RIGHT);
	else if(directionFlags == DirectionFlag::D_RIGHT)
		return static_cast<int8_t>(DPAD_RIGHT);
	else if(directionFlags == (DirectionFlag::D_RIGHT | DirectionFlag::D_DOWN))
		return static_cast<int8_t>(DPAD_DOWN_RIGHT);
	else if(directionFlags == DirectionFlag::D_DOWN)
		return static_cast<int8_t>(DPAD_DOWN);
	else if(directionFlags == (DirectionFlag::D_LEFT | DirectionFlag::D_DOWN))
		return static_cast<int8_t>(DPAD_DOWN_LEFT);
	else if(directionFlags == DirectionFlag::D_LEFT)
		return static_cast<int8_t>(DPAD_LEFT);
	else if(directionFlags == (DirectionFlag::D_LEFT | DirectionFlag::D_UP))
		return static_cast<int8_t>(DPAD_UP_LEFT);
	
	
	return static_cast<int8_t>(DPAD_CENTERED);
}

//------------------------------------------------------------------

// Адаптер для XboxGamepadDevice (общий для контроллеров XBox One S и XBox Series X)
template<detail::XboxGamepadDeviceType CfgType>
XboxDeviceAdapter<CfgType>::XboxDeviceAdapter()
	: directionFlags(0)
	, ptrGamepadDevice()
{
	std::unique_ptr<XboxGamepadDeviceConfiguration> config;
	
	// Разная инициализация в зависимости от CfgType
	if constexpr (CfgType == detail::XboxGamepadDeviceType::One_S)
	{
		config.reset( new XboxOneSControllerDeviceConfiguration() );
	}
	else if constexpr (CfgType == detail::XboxGamepadDeviceType::Series_X)
	{
		// The XBox Series X controller only works on linux kernels >= 6.5
		config.reset( new XboxSeriesXControllerDeviceConfiguration() );
	}
	else
	{
		static_assert(is_supported_xbox_type<CfgType>::value, "Unsupported Xbox gamepad type");
	}

	XboxGamepadDevice* device = new XboxGamepadDevice(config.release());	// Он же и удалит config в деструкторе
	ptrGamepadDevice.reset(device);
}

template<detail::XboxGamepadDeviceType CfgType>
void XboxDeviceAdapter<CfgType>::attachDeviceToParent(BleCompositeHID& parent)
{
	if (ptrGamepadDevice)
		parent.addDevice(ptrGamepadDevice.get());
}

template<detail::XboxGamepadDeviceType CfgType>
void XboxDeviceAdapter<CfgType>::getSpecificAdapterConfiguration(BLEHostConfiguration& bleHostConfig) const
{
	if (!ptrGamepadDevice)
		return;
	
	BLEHostConfiguration idealHostConfig = ptrGamepadDevice->getDeviceConfig()->getIdealHostConfiguration();
	bleHostConfig.setHidType(idealHostConfig.getHidType());
	bleHostConfig.setVidSource(idealHostConfig.getVidSource());
	bleHostConfig.setVid(idealHostConfig.getVid());
	bleHostConfig.setPid(idealHostConfig.getPid());
	bleHostConfig.setGuidVersion(idealHostConfig.getGuidVersion());
	
	bleHostConfig.setSerialNumber(idealHostConfig.getSerialNumber());
}
			

template<detail::XboxGamepadDeviceType CfgType>
void XboxDeviceAdapter<CfgType>::resetButtons()
{
	if (!ptrGamepadDevice)
		return;

	directionFlags = 0;
	ptrGamepadDevice->resetInputs();
}

template<detail::XboxGamepadDeviceType CfgType>
void XboxDeviceAdapter<CfgType>::setButtonState(GamepadButton btn, bool state)
{
	if (!ptrGamepadDevice)
		return;

	switch (btn)
	{
		case GamepadButton::A: case GamepadButton::B:
		case GamepadButton::X: case GamepadButton::Y:
		case GamepadButton::L1: case GamepadButton::R1:
		case GamepadButton::L3: case GamepadButton::R3:
		case GamepadButton::START:		// START на Windows в профиле XBox One S определяется как кнопка R3
		case GamepadButton::HOME:
		{
			uint16_t button_code = getXBoxButtonCode(btn);

			if (state)		ptrGamepadDevice->press(button_code);
			else			ptrGamepadDevice->release(button_code);

			break;
		}

		// Кнопки C и Z в X-Input распознаются только Android, но не на Windows!
		// И только на профиле геймпада XBox Series X
		case GamepadButton::C: case GamepadButton::Z:
		{
			if constexpr (CfgType == detail::XboxGamepadDeviceType::Series_X)
			{
				uint16_t button_code = getXBoxButtonCode(btn);
				if (state)		ptrGamepadDevice->press(button_code);
				else			ptrGamepadDevice->release(button_code);
			}

			break;
		}

		case GamepadButton::SELECT:
		{
			if constexpr (CfgType == detail::XboxGamepadDeviceType::One_S)
			{
				// На Windows определяется как кнопка HOME
				if (state)		ptrGamepadDevice->pressShare();
				else			ptrGamepadDevice->releaseShare();
			}
			else //if constexpr (CfgType == detail::XboxGamepadDeviceType::Series_X)
			{
				uint16_t button_code = getXBoxButtonCode(btn);
				if (state)		ptrGamepadDevice->press(button_code);
				else			ptrGamepadDevice->release(button_code);
			}

			break;
		}

		case GamepadButton::DPAD_BTN_UP:   case GamepadButton::DPAD_BTN_DOWN:
		case GamepadButton::DPAD_BTN_LEFT: case GamepadButton::DPAD_BTN_RIGHT:
		{
			uint8_t flag = getDirectionFlag(btn);

			if (state)	directionFlags |= flag;
			else		directionFlags &= ~flag;

			break;
		}

		case GamepadButton::MENU: case GamepadButton::BACK:
		case GamepadButton::VOLUME_INC: case GamepadButton::VOLUME_DEC: case GamepadButton::VOLUME_MUTE:
		{
			// Not implemented / Not used
			break;
		}
	}
}

template<detail::XboxGamepadDeviceType CfgType>
uint16_t XboxDeviceAdapter<CfgType>::getXBoxButtonCode(GamepadButton btn) const
{
	switch (btn)
	{
		case GamepadButton::A:			return static_cast<uint16_t>(XBOX_BUTTON_A);
		case GamepadButton::B:			return static_cast<uint16_t>(XBOX_BUTTON_B);
		case GamepadButton::C:
			if constexpr (CfgType == detail::XboxGamepadDeviceType::Series_X)		return static_cast<uint16_t>(0x04);		// 0x04 - UNUSED (Button 3)
			else																	break;
		case GamepadButton::X:			return static_cast<uint16_t>(XBOX_BUTTON_X);
		case GamepadButton::Y:			return static_cast<uint16_t>(XBOX_BUTTON_Y);
		case GamepadButton::Z:
			if constexpr (CfgType == detail::XboxGamepadDeviceType::Series_X)		return static_cast<uint16_t>(0x20);		// 0x20 - UNUSED (Button 6)
			else																	break;
		case GamepadButton::L1:			return static_cast<uint16_t>(XBOX_BUTTON_LB);
		case GamepadButton::R1:			return static_cast<uint16_t>(XBOX_BUTTON_RB);
		case GamepadButton::L3:			return static_cast<uint16_t>(XBOX_BUTTON_LS);
		case GamepadButton::R3:			return static_cast<uint16_t>(XBOX_BUTTON_RS);
		case GamepadButton::SELECT:
			if constexpr (CfgType == detail::XboxGamepadDeviceType::Series_X)		return static_cast<uint16_t>(XBOX_BUTTON_SELECT);
			else																	break;
		case GamepadButton::START:		return static_cast<uint16_t>(XBOX_BUTTON_START);
		case GamepadButton::HOME:		return static_cast<uint16_t>(XBOX_BUTTON_HOME);

		// 0x100 - XBox One S на ПК как Select
		// 0x200 - XBox One S на ПК как Start
	}

	return static_cast<uint16_t>(-1);
}

template<detail::XboxGamepadDeviceType CfgType>
uint8_t XboxDeviceAdapter<CfgType>::getDirectionFlag(GamepadButton btn) const
{
	switch (btn)
	{
		case GamepadButton::DPAD_BTN_UP:		return static_cast<uint8_t>(XboxDpadFlags::NORTH);
		case GamepadButton::DPAD_BTN_DOWN:		return static_cast<uint8_t>(XboxDpadFlags::SOUTH);
		case GamepadButton::DPAD_BTN_LEFT:		return static_cast<uint8_t>(XboxDpadFlags::WEST);
		case GamepadButton::DPAD_BTN_RIGHT:		return static_cast<uint8_t>(XboxDpadFlags::EAST);
	}

	return static_cast<uint8_t>(XboxDpadFlags::NONE);
}


template<detail::XboxGamepadDeviceType CfgType>
void XboxDeviceAdapter<CfgType>::setLeftThumb(int16_t leftX, int16_t leftY)
{
	if (!ptrGamepadDevice)
		return;

	ptrGamepadDevice->setLeftThumb(leftX, leftY);
}

template<detail::XboxGamepadDeviceType CfgType>
void XboxDeviceAdapter<CfgType>::setRightThumb(int16_t rightX, int16_t rightY)
{
	if (!ptrGamepadDevice)
		return;

	ptrGamepadDevice->setRightThumb(rightX, rightY);
}

template<detail::XboxGamepadDeviceType CfgType>
void XboxDeviceAdapter<CfgType>::setLeftTrigger(int16_t l2)
{
	if (!ptrGamepadDevice)
		return;

	uint16_t correct_l2 = static_cast<uint16_t>(l2);
	ptrGamepadDevice->setLeftTrigger(correct_l2);
}

template<detail::XboxGamepadDeviceType CfgType>
void XboxDeviceAdapter<CfgType>::setRightTrigger(int16_t r2)
{
	if (!ptrGamepadDevice)
		return;

	uint16_t correct_r2 = static_cast<uint16_t>(r2);
	ptrGamepadDevice->setRightTrigger(correct_r2);
}


template<detail::XboxGamepadDeviceType CfgType>
void XboxDeviceAdapter<CfgType>::sendGamepadReport()
{
	if (!ptrGamepadDevice)
		return;

	//const uint8_t dpadState = dPadDirectionToValue( static_cast<XboxDpadFlags>(directionFlags) );
	//ptrGamepadDevice->pressDPadDirection(dpadState);
	ptrGamepadDevice->pressDPadDirectionFlag( static_cast<XboxDpadFlags>(directionFlags) );

	ptrGamepadDevice->sendGamepadReport();
}


// ЯВНОЕ ИНСТАНЦИРОВАНИЕ
template class XboxDeviceAdapter<detail::XboxGamepadDeviceType::One_S>;
template class XboxDeviceAdapter<detail::XboxGamepadDeviceType::Series_X>;