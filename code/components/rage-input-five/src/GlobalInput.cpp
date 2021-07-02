// code adapted from https://github.com/mumble-voip/mumble/blob/cc291eee2e0407f6b8d170d1aa5d43c9c7b4b18b/src/mumble/GlobalShortcut_win.cpp
// Copyright 2007-2021 The Mumble Developers. All rights reserved.

#include <StdInc.h>
#include <GlobalInput.h>

#include <hidsdi.h>
#include <hidpi.h>

struct GlobalInputHandlerLocal : GlobalInputHandler, std::enable_shared_from_this<GlobalInputHandlerLocal>
{
	GlobalInputHandlerLocal();

	~GlobalInputHandlerLocal();

private:
	void Create();

	LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

	void KeyboardMessage(const uint16_t flags, uint16_t scanCode, const uint16_t virtualKey);

private:
	HWND hWnd = NULL;
	std::thread thread;
};

GlobalInputHandlerLocal::GlobalInputHandlerLocal()
{
	thread = std::thread([this]()
	{
		Create();

		while (hWnd)
		{
			MSG msg = { 0 };

			if (GetMessage(&msg, NULL, 0, 0))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	});
}

LRESULT GlobalInputHandlerLocal::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_CREATE:
		{
			constexpr uint8_t nRid = 1;
			RAWINPUTDEVICE rid[nRid] = {};

			rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
			rid[0].usUsage = HID_USAGE_GENERIC_KEYBOARD;
			rid[0].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
			rid[0].hwndTarget = hWnd;

			if (!RegisterRawInputDevices(rid, nRid, sizeof(RAWINPUTDEVICE)))
			{
				trace("RegisterRawInputDevices() failed with error %u!\n", GetLastError());
			}

			break;
		}

		case WM_INPUT:
		{
			auto handle = reinterpret_cast<HRAWINPUT>(lParam);
			UINT size = 0;
			if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0)
			{
				break;
			}

			auto buffer = std::make_unique<uint8_t[]>(size);
			if (GetRawInputData(handle, RID_INPUT, buffer.get(), &size, sizeof(RAWINPUTHEADER)) <= 0)
			{
				break;
			}

			auto input = reinterpret_cast<const PRAWINPUT>(buffer.get());
			switch (input->header.dwType)
			{
				case RIM_TYPEMOUSE:
				{
					const RAWMOUSE& mouse = input->data.mouse;
					// no-op
					break;
				}
				case RIM_TYPEKEYBOARD:
				{
					const RAWKEYBOARD& keyboard = input->data.keyboard;
					if (keyboard.MakeCode == KEYBOARD_OVERRUN_MAKE_CODE)
					{
						// Invalid or unrecognizable combination of keys is pressed or
						// the number of keys pressed exceeds the limit for this keyboard.
						break;
					}

					if (keyboard.VKey == 0xFF)
					{
						// Discard "fake keys" which are part of an escaped sequence.
						break;
					}

					KeyboardMessage(keyboard.Flags, keyboard.MakeCode, keyboard.VKey);
					break;
				}
				case RIM_TYPEHID:
				{
					const RAWHID& hid = input->data.hid;
					std::vector<char> reports(hid.dwSizeHid * hid.dwCount);
					memcpy(reports.data(), hid.bRawData, reports.size());

					// no-op
				}
			}

			break;
		}
		case WM_DESTROY:
			hWnd = NULL;
			break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void GlobalInputHandlerLocal::KeyboardMessage(const uint16_t flags, uint16_t scanCode, const uint16_t virtualKey)
{
	OnKey(virtualKey, !(flags & RI_KEY_BREAK));
}

void GlobalInputHandlerLocal::Create()
{
	static std::map<HWND, std::weak_ptr<GlobalInputHandlerLocal>> windowMap;

	WNDCLASS wc = { 0 };
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT
	{
		if (uMsg == WM_CREATE)
		{
			auto owner = reinterpret_cast<GlobalInputHandlerLocal*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
			windowMap[hWnd] = owner->shared_from_this();
			owner->hWnd = hWnd;
		}

		if (auto entry = windowMap.find(hWnd); entry != windowMap.end())
		{
			if (auto wnd = entry->second.lock())
			{
				return wnd->WndProc(uMsg, wParam, lParam);
			}
		}

		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	};
	wc.lpszClassName = L"Cfx_GlobalInputWindow";

	RegisterClass(&wc);

	CreateWindow(L"Cfx_GlobalInputWindow", L"CitizenFX Global Input Window", 0, 0, 0, 1, 1, NULL, NULL, GetModuleHandle(NULL), this);
}

GlobalInputHandlerLocal::~GlobalInputHandlerLocal()
{
	if (hWnd)
	{
		DestroyWindow(hWnd);
	}

	if (thread.joinable())
	{
		thread.join();
	}
}

std::shared_ptr<GlobalInputHandler> CreateGlobalInputHandler()
{
	return std::make_shared<GlobalInputHandlerLocal>();
}
