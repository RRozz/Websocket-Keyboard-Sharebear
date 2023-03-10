#define _WEBSOCKETPP_CPP11_THREAD_
#define _WEBSOCKETPP_CPP11_FUNCTIONAL_
#define _WEBSOCKETPP_CPP11_SYSTEM_ERROR_
#define _WEBSOCKETPP_CPP11_RANDOM_DEVICE_
#define _WEBSOCKETPP_CPP11_MEMORY_

// RESUME -- ^above are the recommended headers for 2013 to enable c++ 11 for websocket++. maybe this will result in faster builds?
// maybe try getting rid of the boost includes / things if this doesn't work

#include <iostream>
#include <websocketpp/config/asio_no_tls.hpp>
#include <oleacc.h> // for COM initialization & win hook

#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

typedef websocketpp::server<websocketpp::config::asio> server;
typedef websocketpp::client<websocketpp::config::asio_client> client;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

// pull out the type of messages sent by our config
typedef server::message_ptr message_ptr;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr_c;

// TIP: to start with command-line arguments in debugging, go to and change:
// Project proerties -> Debugging -> Command Line Arguments

// P.S. this project seemed to make Windows Search Indexer act funny (kept restarting and taking 50% cpu)
// so i just disabled the Windows Search service

// NOTE TO SELF: if sending a message with opcode ::text, it will crash the client if the message contains ? or / or ;, etc.
// it seems to be safe to use ::binary

/**
SO websocket controller packet messages
key: 00 00/01(down/up)      00~FF (the message... just down-cast WPARAM into char... all key codes are under FF)
sys key: 01 00/01               00~FF
mouse move: 02 00~FF(x % pos int)  00~FF (x % pos float as char)   00~FF (y % int)   00~FF (y % float)
mouse click: 03 00/01(down/up)      00~02(left = 0, right = 1, middle = 2)
mouse wheel: 04 00/01(roll down/up) 00~FF (potency / how many roll increments)
=== PLUS ===
clipboard text: 05  any and all clipboard text (as hex) starting from second byte
special stuff: 06 xx (includes server clipboard copy request, quit, benchmark, etc), xx is the second byte specifying the action
**/

// how many times per second client will scan for mouse position
#define MOUSE_SCAN_FPS 30 // RESUME -- make this a global var so user can edit l8r with cin
#define MOUSE_SCAN_IDLE_FPS 5

using namespace std;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelKeyboardProc(INT nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK server_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void getRectSize();

websocketpp::connection_hdl connHdl;
string role = "";
int myPort = 0;
string serverIP = "";
string clientURI = "";
server echo_server;
client c;
bool session_start_success = false;
bool session_stopped = false;
bool done_starting = false;
bool clientMode = false;
bool scanRun = true; // when false, scan thread will stop
bool mouse_scanRun = true;
std::thread *mySesh = 0;
HWND hwnd = 0;
HBRUSH hbr_backgroundColor;
std::thread scan;
std::thread mouse_scan;
RECT windowPos;
bool hasFocus = false; // used to determine when to not scan for mouse input
bool isMinimized = false; // used to determine when client window is minimized and needs to be restored on RWIN key
bool connected = false; // client will crash without connection; this is used to check
HHOOK mule;
HHOOK g_hKeyboardHook;
bool hadKeys[255] = { false };
bool mo_cap_enabled = false; // capture all mouse input, not just when client window has focus
int sleepTime = (1000 / MOUSE_SCAN_FPS); // for mouse scan fps
int idle_sleepTime = (1000 / MOUSE_SCAN_IDLE_FPS);
POINT lastmouse; // previous mouse coords
bool server_to_client_clipboard = false; // if this is true, then server ctrl+C 'd something. don't want client to needlessly send same clipboard data back to server and create infinite loop
WNDPROC server_Proc; // used to set server to use a different function to handle messages; instead of WindowProc, it will use server_WndProc
WNDPROC client_Proc;
bool clipboard_active = false; // whether or not the clipboard will be 'on' / will transfer clipboard data (if text) on cb change, toggle via console

// vars that may or may not be temporary parts of the program
int bm_prog = 0; // for benchmarking latency
int bm_prog_t[5] = { 0 };

string createURI(string serverIP, int port){
	string out = "ws://";
	out += serverIP;
	out += ":";
	out += std::to_string(port);
	return out;
}

/*
void toClipboard(string new_text){
	OpenClipboard(0);
	EmptyClipboard();
	HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, new_text.size() + 1);
	if (!hg){
		CloseClipboard();
		return;
	}
	memcpy(GlobalLock(hg), new_text.c_str(), new_text.size() + 1);
	GlobalUnlock(hg);
	SetClipboardData(CF_TEXT, hg);
	CloseClipboard();
	GlobalFree(hg);
}
legacy ASCII version
*/

void toClipboard(wstring new_text){
	OpenClipboard(0);
	EmptyClipboard();
	HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (new_text.size() * 2) + 2); // 2 bytes per char, plus 2-byte null terminating char
	if (!hg){
		CloseClipboard();
		return;
	}
	memcpy(GlobalLock(hg), new_text.c_str(), (new_text.size() * 2) + 2);
	GlobalUnlock(hg);
	SetClipboardData(CF_UNICODETEXT, hg);
	CloseClipboard();
	GlobalFree(hg);
}

/*string fromClipboard(){
	string out = "";
	if (OpenClipboard(nullptr)){
		HANDLE hData = GetClipboardData(CF_TEXT); // CF_UNICODETEXT will only put 1 char into the char *pszText, CF_TEXT will work normally
		if (hData != nullptr){
			char *pszText = static_cast<char*>(GlobalLock(hData));
			cout << "pszText = " << pszText << endl;
			if (pszText != nullptr){
				out = pszText;
			}
			else{
				out = "pszText = nullptr";
			}
			GlobalUnlock(hData);
		}
		else{
			out = "hData = nullptr";
		}
		CloseClipboard();
	}
	else{
		out = "failed to open clipboard";
	}
	return out;
}
// legacy ASCII version
*/

wstring fromClipboard(){ // now with wchar_t support
	wstring out = L"";
	if (OpenClipboard(nullptr)){
		HANDLE hData = GetClipboardData(CF_UNICODETEXT);
		if (hData != nullptr){
			wchar_t *pszText = static_cast<wchar_t*>(GlobalLock(hData));
			//wcout << L"pszText = " << pszText << endl;
			if (pszText != nullptr){
				out = pszText;
			}
			else{
				out = L"pszText = nullptr";
			}
			GlobalUnlock(hData);
		}
		else{
			out = L"hData = nullptr";
		}
		CloseClipboard();
	}
	else{
		out = L"failed to open clipboard";
	}
	return out;
}
const char *hexChars = "0123456789ABCDEF";
string encodeHex(std::wstring wstr){ // because we can only use std::string as an arg for websocketpp's send(). Encode wstring as hex.
	string o = "";
	for (int xint = 0, len = wstr.length(); xint < len; xint++){
		// high order byte of wchar_t
		unsigned char c1 = (wstr[xint] & 65280) >> 8;
		unsigned char c1_left = hexChars[c1 / 16];
		unsigned char c1_right = hexChars[c1 % 16];
		// low order byte of wchar_t
		unsigned char c2 = wstr[xint] & 255;
		unsigned char c2_left = hexChars[c2 / 16];
		unsigned char c2_right = hexChars[c2 % 16];
		o += c1_left;
		o += c1_right;
		o += c2_left;
		o += c2_right;
	}
	return o;
}

wstring decodeHex(std::string str){ // turn (uppercase) hex string back into wstring
	wstring o = L"";
	int len = str.length();
	if ((len & 1) == 1) return L"[Odd num of chars, not hex]";
	if ((len % 4) != 0) return L"[Length wrong for wstring, can't de-hex]"; // need 4 hex letters per wchar_t
	for (int xint = 0; xint < len; xint++){
		unsigned char HI_left; // high order byte of wchar_t, left hex char
		unsigned char HI_right;
		unsigned char LO_left; // low order byte of wchar_t, left hex char
		unsigned char LO_right;

		// char 1, hex 1
		if (str[xint] >= 'A' && str[xint] <= 'F'){
			HI_left = 10 + (str[xint++] - 'A');
		}
		else if (str[xint] >= '0' && str[xint] <= '9'){
			HI_left = str[xint++] - '0';
		}
		else{
			return L"[Char invalid, not hex]"; // only accepting capital A-F & 0-9 as hex
		}

		// char 1, hex 2
		if (str[xint] >= 'A' && str[xint] <= 'F'){
			HI_right = 10 + (str[xint++] - 'A');
		}
		else if (str[xint] >= '0' && str[xint] <= '9'){
			HI_right = str[xint++] - '0';
		}
		else{
			return L"[Char invalid, not hex]";
		}

		// char 2, hex 1
		if (str[xint] >= 'A' && str[xint] <= 'F'){
			LO_left = 10 + (str[xint++] - 'A');
		}
		else if (str[xint] >= '0' && str[xint] <= '9'){
			LO_left = str[xint++] - '0';
		}
		else{
			return L"[Char invalid, not hex]";
		}

		// char 2, hex 2
		if (str[xint] >= 'A' && str[xint] <= 'F'){
			LO_right = 10 + (str[xint] - 'A');
		}
		else if (str[xint] >= '0' && str[xint] <= '9'){
			LO_right = str[xint] - '0';
		}
		else{
			return L"[Char invalid, not hex]";
		}


		wchar_t fin = ((HI_left * 16) + HI_right) << 8; // assemble high order byte
		fin += (LO_left * 16) + LO_right; // assemble low order byte
		o += fin; // add decoded wchar_t to wstring
	}
	return o;
}

void forceShowWindow(HWND hwnd, DWORD showType=5) {
	DWORD windowThreadProcessId = GetWindowThreadProcessId(GetForegroundWindow(), LPDWORD(0));
	DWORD currentThreadId = GetCurrentThreadId();
	AttachThreadInput(windowThreadProcessId, currentThreadId, true);
	BringWindowToTop(hwnd);
	ShowWindow(hwnd, showType);
	AttachThreadInput(windowThreadProcessId, currentThreadId, false);
}


// Define a callback to handle incoming messages
void on_message(server* s, websocketpp::connection_hdl hdl, message_ptr msg) {

	// may have to replace mouse_event (and maybe keybd_event) with SendInput() for Windows 10
	// SendInupt will use UIPI and need to have admin privileges to send to admin-running programs
	// https://stackoverflow.com/questions/5164774/how-can-i-simulate-mouse-events-from-code

	string in = msg->get_payload();
	int inl = in.length();
	if (inl > 2){
		if (in[0] == 0){
			//cout << " key is ";
			if (in[1]){
				//cout << "up";
				keybd_event(in[2], 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
			}
			else{
				//cout << "down";
				keybd_event(in[2], 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
			}
			//cout << endl;
		}
		else if (in[0] == 1)
		{
			//cout << "SYS key " << (int)in[2] << " is ";
			if (in[1]){
				//cout << "up";
				keybd_event(in[2], 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
			}
			else{
				//cout << "down";
				keybd_event(in[2], 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
			}
			//cout << endl;
		}
		else if (in[0] == 2){
			if (inl > 4){
				// cout << "mouse moved" << endl;
				// coords are transferred as 2 bytes each, 0 to 65535 for each x and y

				// x byte 1 = last byte (0000 xxxx), byte 2 = first  byte / higher-order bits (xxxx 0000)
				// same for y

				DWORD x_true = (unsigned char)in[1];
				x_true += ((unsigned char)in[2]) << 8;
				DWORD y_true = (unsigned char)in[3];
				y_true += ((unsigned char)in[4]) << 8;

				//DWORD x_true = (((float)in[1] / 100.0f) * 65535);
				//DWORD y_true = (((float)in[3] / 100.0f) * 65535);

				//cout << "True X: " << x_true << ", Y: " << y_true << endl;

				mouse_event(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, x_true, y_true, 0, 0);
				//mouse_event(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, x_final, y_final, 0, 0);

				/**
				cout << "Translated mouse to... X: " << x_final << ", Y: " << y_final << endl;
				cout << "Message received: ";
				for (int xint = 0, l = in.length(); xint < l; xint++){
				cout << " [" << (int)in[xint] << "] ";
				}
				cout << endl;
				**/
			}
			else{
				cout << "mouse movement message too small." << endl;
			}
		}
		else if (in[0] == 3){
			//cout << "mouse clicked" << endl;
			if (in[1]){
				if (in[2] == 0){
					//cout << "left click up" << endl;
					mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
				}
				else if (in[2] == 1){
					//cout << "right click up" << endl;
					mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
				}
				else{
					//cout << "middle click up" << endl;
					mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
				}
			}
			else{
				if (in[2] == 0){
					//cout << "left click down" << endl;
					mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
				}
				else if (in[2] == 1){
					//cout << "right click down" << endl;
					mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
				}
				else{
					//cout << "middle click down" << endl;
					mouse_event(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0, 0);
				}
			}
		}
		else if (in[0] == 4){
			if (in[1]){
				// mouse wheel scroll up (away from user)
				// mousedata is positive number. 0 + 120 per cycle/rotation of wheel
				//cout << "mouse wheel : scroll up (& md = " << 120 * in[2] << " )" << endl;
				INPUT ip;
				ip.type = INPUT_MOUSE;
				ip.mi.dwFlags = MOUSEEVENTF_WHEEL;
				ip.mi.mouseData = 120 * in[2];
				ip.mi.time = 0;
				SendInput(1, &ip, sizeof(ip));
			}
			else{
				// scroll down
				// mousedata is negative number(or at least high order bit is 1), then minus 120 per cycle of wheel
				//cout << "mouse wheel : scroll up (& md = " << 120 * in[2] << " )" << endl;
				INPUT ip;
				ip.type = INPUT_MOUSE;
				ip.mi.dwFlags = MOUSEEVENTF_WHEEL;
				ip.mi.mouseData = 0 - (120 + in[2]); // 65296 + (120 + in[2]) acts as super jump upward scroll, can make super scroll macro this way
				ip.mi.time = 0;
				SendInput(1, &ip, sizeof(ip));
			}
		}
		else if (in[0] == 5){
			wstring new_clipboard = decodeHex(in.substr(1));
			toClipboard(new_clipboard);
			//cout << "received clipboard data" << endl;
		}
		else if (in[0] == 6){
			// 6 was supposed to be special operations like ALT+TAB but i don't think i ever used it, just sent TAB when ALT was down
			if (in[1] == 0){
				//cout << "Simming ALT + TAB" << endl;
				if (in[2] == 0){ // forwards tab
					keybd_event(VK_LMENU, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
					Sleep(5);
					keybd_event(VK_TAB, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
					Sleep(5);
					keybd_event(VK_TAB, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
					Sleep(5);
					keybd_event(VK_LMENU, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
				}
				else{ // backwards tab... not working
					keybd_event(VK_LMENU, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
					Sleep(5);
					keybd_event(VK_LSHIFT, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
					Sleep(5);
					keybd_event(VK_TAB, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
					Sleep(5);
					keybd_event(VK_TAB, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
					Sleep(5);
					keybd_event(VK_LSHIFT, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
					Sleep(5);
					keybd_event(VK_LMENU, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
				}
			}
			else if (in[1] == 1){
				SYSTEMTIME st;
				GetSystemTime(&st);
				int tmp = st.wMinute * (60 * 1000);
				tmp += st.wSecond * 1000;
				tmp += st.wMilliseconds;
				bm_prog_t[bm_prog] = tmp;
				if (!bm_prog) cout << "\nBenchmark started\n============" << endl;
				cout << "[ " << bm_prog << " ] : " << bm_prog_t << endl;
				bm_prog++;
				if (bm_prog == 5){
					cout << "\nBenchmark Complete" << endl;
					cout << "============\nResults:" << endl;
					int x = (bm_prog_t[1] - bm_prog_t[0]);
					x += (bm_prog_t[2] - bm_prog_t[1]);
					x += (bm_prog_t[3] - bm_prog_t[2]);
					x += (bm_prog_t[4] - bm_prog_t[3]);
					x /= 4;
					cout << "Average latency: " << x << endl;
					cout << "\n0 -> 1 ::: " << (bm_prog_t[1] - bm_prog_t[0]) << endl;
					cout << "1 -> 2 ::: " << (bm_prog_t[2] - bm_prog_t[1]) << endl;
					cout << "2 -> 3 ::: " << (bm_prog_t[3] - bm_prog_t[2]) << endl;
					cout << "3 -> 4 ::: " << (bm_prog_t[4] - bm_prog_t[3]) << endl;
					bm_prog = 0;
				}
			}
			else if (in[1] == 2){
				// mutual quit, close server
				cout << "received special message to quit... quitting." << endl;

				DWORD dwTmp;
				INPUT_RECORD ir[3];
				HANDLE hConIn;
				hConIn = GetStdHandle(STD_INPUT_HANDLE);

				ir[0].EventType = KEY_EVENT;
				ir[0].Event.KeyEvent.bKeyDown = TRUE;
				ir[0].Event.KeyEvent.dwControlKeyState = 0;
				ir[0].Event.KeyEvent.uChar.UnicodeChar = VK_RETURN;
				ir[0].Event.KeyEvent.wRepeatCount = 1;
				ir[0].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
				ir[0].Event.KeyEvent.wVirtualScanCode = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);

				ir[1].EventType = KEY_EVENT;
				ir[1].Event.KeyEvent.bKeyDown = TRUE;
				ir[1].Event.KeyEvent.dwControlKeyState = 0;
				ir[1].Event.KeyEvent.uChar.UnicodeChar = 'q';
				ir[1].Event.KeyEvent.wRepeatCount = 1;
				ir[1].Event.KeyEvent.wVirtualKeyCode = 'Q'; // might have to be UPPERCASE?
				ir[1].Event.KeyEvent.wVirtualScanCode = MapVirtualKey('Q', MAPVK_VK_TO_VSC);

				ir[2].EventType = KEY_EVENT;
				ir[2].Event.KeyEvent.bKeyDown = TRUE;
				ir[2].Event.KeyEvent.dwControlKeyState = 0;
				ir[2].Event.KeyEvent.uChar.UnicodeChar = VK_RETURN;
				ir[2].Event.KeyEvent.wRepeatCount = 1;
				ir[2].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
				ir[2].Event.KeyEvent.wVirtualScanCode = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);

				WriteConsoleInput(hConIn, ir, 3, &dwTmp);
				cout << "quit done" << endl;
			}else if(in[1] == 3){ // client requesting clipboard from the server
				// to send a response from server to client, we get a connection from the server based on the handle
				server::connection_ptr con = s->get_con_from_hdl(hdl);
				string response = "";
				response += (char)5;
				response += encodeHex(fromClipboard());
				con->send(response, websocketpp::frame::opcode::binary);
				//cout << "Sent clipboard to client." << endl;
			}
		}
		else{
			cout << ">>> Message not understood... (opcode = '" << (int)in[0] << "') <<<" << endl;
		}
	}


	// check for a special command to instruct the server to stop listening so
	// it can be cleanly exited.
	/*if (msg->get_payload() == "stop-listening") {
		s->stop_listening();
		return;
	}
	*/

	/**
	try {
	s->send(hdl, msg->get_payload(), msg->get_opcode());
	}
	catch (websocketpp::exception const & e) {
	std::cout << "Echo failed because: "
	<< "(" << e.what() << ")" << std::endl;
	}
	**/
}
void client_on_message(client* c, websocketpp::connection_hdl hdl, message_ptr_c msg) {
	//std::cout << "on_message called with hdl: " << hdl.lock().get()
	//	<< " and message: " << msg->get_payload()
	//	<< std::endl;

	string in = msg->get_payload();
	int inl = in.length();

	if (inl > 1){
		if (in[0] == 5){
			server_to_client_clipboard = true;
			toClipboard( decodeHex( in.substr(1) ) );
			//cout << "received clipboard text from server" << endl;
			server_to_client_clipboard = false;
		}
		else{
			cout << "Received unknown message from server" << endl;
		}
	}
	else{
		cout << "message too short, only : " << inl << endl; // RESUME remove after getting ctrl-shift-c working
	}

	websocketpp::lib::error_code ec;

	//c->send(hdl, msg->get_payload(), msg->get_opcode(), ec);
	if (ec) {
		std::cout << "Echo failed because: " << ec.message() << std::endl;
	}
}

void on_open(client* c, websocketpp::connection_hdl hdl) {
	connHdl = hdl; // save connection handle so we can send messages from other places
	hbr_backgroundColor = GetSysColorBrush(WHITE_BRUSH);
	connected = true;
	if (clientMode){
		cout << "Client has successfully connected to server!" << endl;
	}
	else{
		cout << "Server has received connection from client!" << endl;
	}
	//std::string msg = "Hello";
	//c->send(hdl, msg, websocketpp::frame::opcode::binary);
	//c->get_alog().write(websocketpp::log::alevel::app, "Sent Message: " + msg);
}
void client_on_close(client* c, websocketpp::connection_hdl hdl){
	cout << "Lost connection with server...\nPlease restart client while server is running." << endl;
	mouse_scanRun = false;
	scanRun = false;
	PostMessage(hwnd, WM_CLOSE, 0, 0);
}

void cleanup(){
	if (!session_stopped){
		if (clientMode){
			c.stop();
			SendMessage(hwnd, WM_CLOSE, 0, NULL);
			DeleteObject(hbr_backgroundColor);
		}
		else{
			echo_server.stop();
		}
		mySesh->join();
		if (clientMode){
			scanRun = false;
			scan.join();
			mouse_scanRun = false;
			mouse_scan.join();
		}
		else{
			scan.join();
		}
	}
	if (!clientMode){
		for (int xint = 0; xint < 255; xint++){
			if (hadKeys[xint])
				keybd_event(xint, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
		}
		mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
		mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
		mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
	}
	else{
		UnhookWindowsHookEx(g_hKeyboardHook);
		ReleaseCapture();
	}
}

void start_session(){
	try {
		// Set logging settings
		//echo_server.set_access_channels(websocketpp::log::alevel::all);
		//echo_server.clear_access_channels(websocketpp::log::alevel::frame_payload);

		// Initialize Asio

		// Register our message handler
		if (role == "server" || role == "s"){
			//echo_server.set_access_channels(websocketpp::log::alevel::none);
			//echo_server.clear_access_channels(websocketpp::log::alevel::none);
			echo_server.set_access_channels(websocketpp::log::alevel::none);
			echo_server.clear_access_channels(websocketpp::log::alevel::none);
			echo_server.init_asio();
			echo_server.set_message_handler(bind(&on_message, &echo_server, ::_1, ::_2));
			// Listen on port 9002
			echo_server.listen(myPort);
			// Start the server accept loop
			echo_server.start_accept();
			// Start the ASIO io_service run loop
			session_start_success = true;
			done_starting = true;
			echo_server.run();
			cout << "Server stopped." << endl;
		}
		else{
			c.set_access_channels(websocketpp::log::alevel::none);
			c.clear_access_channels(websocketpp::log::alevel::none);
			c.init_asio();
			c.set_open_handler(bind(&on_open, &c, ::_1));
			c.set_message_handler(bind(&client_on_message, &c, ::_1, ::_2));
			c.set_close_handler(bind(&client_on_close, &c, ::_1)); // upon d/c, stop sending mouse messages so the client doesn't crash right away
			websocketpp::lib::error_code ec;
			client::connection_ptr con = c.get_connection(clientURI, ec);
			if (ec) {
				std::cout << "could not create connection because: " << ec.message() << std::endl;
				done_starting = true;
				return;
			}
			done_starting = true;
			session_start_success = true;
			clientMode = true;
			c.connect(con);
			// don't send message without run() or it will be 'Bad Connection' and end program
			//string x = "bla";
			//c.send(connHdl, x, websocketpp::frame::opcode::binary);
			c.run();
			cout << "Client stopped." << endl;
		}
	}
	catch (websocketpp::exception const & e) {
		std::cout << e.what() << std::endl;
	}
	catch (...) {
		std::cout << "other exception" << std::endl;
	}
}

int main(int argc, const char* argv[]){


	// use this program for both client and server... which am i? use user input
	if (argc > 1){ // first check for arguments... 
		// argv[0] is program dir, argv[1] should be 'client' or 'server'
		if (argc > 2){
			role = string(argv[1]);
			if (role == "server" || role == "s" || role == "client" || role == "c"){
				myPort = std::stoi(argv[2]); // argv[2] is port
				if (!myPort){
					cout << "Improper port. Changing port to 9002" << endl;
					myPort = 9002;
				}
				if (role == "client" || role == "c"){
					if (argc > 3){
						serverIP = argv[3]; // argv[3] is server IP (only for client)
						cout << "Connecting to server @ " << serverIP << ":" << myPort << endl;
					}
					else{
						cout << "As client, you must specify the server IP in third argument. Cannot start program." << endl;
						system("pause");
						return -3;
					}
				}
				else{
					cout << "Acting as server, awaiting connections on port " << myPort << endl;
				}
			}
			else{
				cout << "Improper role. Cannot start program" << endl;
				system("pause");
				return -2;
			}
		}
		else{
			cout << "Not enough arguments to start program..." << endl;
			cout << "First arg: 'client' ('c') or 'server' or ('s')" << endl;
			cout << "Second arg: port" << endl;
			cout << "Third arg (if client) : server IP" << endl;
			cout << "\nPlease enter valid arguments or start program without any arguments.\n" << endl;
			system("pause");
			return -1;
		}
	}
	else{ // if no args, ask user via console...
		cout << "Hello, there were no arguments entered for websocket Controller's role" << endl;
		cout << "Who would you like to be : 'client' ('c') or 'server' ('s') ?" << endl;
		getline(cin, role);
		if (role == ""){
			cout << "nothing entered, assuming server role." << endl;
			role = "server";
		}
		else if (role != "c" && role != "s" && role != "client" && role != "server"){
			cout << "incorrent input... assuming server role." << endl;
			role = "server";
		}
		else{
			if (role == "server" || role == "s"){
				cout << "You will be the server!" << endl;
			}
			else{
				cout << "You will be the client!" << endl;
			}
		}
		cout << "Now please provide the port on which you would like to communicate..." << endl;
		string tmp = "";
		getline(cin, tmp);
		myPort = std::stoi(tmp);
		if (myPort != 0){
			cout << "You will work on port " << myPort << "!" << endl;
		}
		else{
			cout << "Your port input was not valid... Your port will be set to 9002!" << endl;
			myPort = 9002;
		}
		if (role == "client" || role == "c"){
			cout << "Now please input the IP of the server who will be controlled" << endl;
			getline(cin, serverIP);
			char dotFound = 0;
			bool no_letters = true;
			for (int xint = 0, l = serverIP.length(); xint < l; xint++){
				if (serverIP[xint] == '.')dotFound++;
				else if (serverIP[xint] < '0' || serverIP[xint] > '9') no_letters = false;
			}
			if (dotFound == 3 && no_letters){
				cout << "Server IP seems correct. Will attempt to connect to " << serverIP << "." << endl;
			}
			else{
				if (!no_letters){
					cout << "Server IP must contain only letters! Invalid IP cannot be reached." << endl;
				}
				else if (dotFound != 3){
					cout << "Server IP must have 4 octets! Invalid IP cannot be reached." << endl;
				}
				cout << "ServerIP has been changed to 0.0.0.0" << endl;
				serverIP = "0.0.0.0";
			}
		}
	}

	clientURI = createURI(serverIP, myPort);


	std::thread sesh(start_session);
	mySesh = &sesh;
	std::atexit(cleanup);

	cout << "Done? " << done_starting << endl;

	cout << "Waiting for start..." << endl;

	Sleep(1000);

	if (!done_starting) Sleep(1000);
	if (clientMode && !connected){
		cout << "WARNING: connection is null. The client will crash if the connection is null." << endl;
		cout << "You may either quit now or wait for the server to start up and continue." << endl;
		cout << "c = continue\nanything else = quit" << endl;
		cout << "Your choice: ";
		string tmp = "";
		getline(cin, tmp);
		bool quit = false;
		if (tmp != "c"){
			cout << "ur input : '" << tmp << "'" << endl;
			quit = true;
		}
		else{
			cout << "Continuing..." << endl;
			// dangerous? been a while since i touched this part. dk if reassigning sesh is safe
			// yeah, says 'called from' somewhere it shouldn't have been called. then the app crashes
			//sesh = std::thread(start_session);
			//mySesh = &sesh;
			//cout << "Waiting for 3 seconds..." << endl;
			//Sleep(3000);
			cout << "After continue: ";
			if (connected){
				cout << "client has now connected to server!" << endl;
			}
			else{
				cout << "client has still not connected to server." << endl;
				cout << "Second chance..." << endl;
				cout << "c = continue\nanything else = quit" << endl;
				cout << "Your choice: ";
				string tmp = "";
				getline(cin, tmp);
				if (tmp != "c"){
					cout << "ur input : '" << tmp << "'" << endl;
					quit = true;
				}
			}
		}
		if (quit){
			cout << "\nQuitting..." << endl;
			c.stop();
			sesh.join();
			session_stopped = true;
			cout << "Successfully quit early after client failed to connect in order to avoid crash!" << endl;
			cout << "bye bye... better luck next time!\n" << endl;
			system("pause");
			return 1;
		}
	}

	// message-getting loop for server
	auto lam_server = [](){
		const wchar_t CLASS_NAME[] = L"Sample Window Class";
		WNDCLASS wc = {};

		wc.lpfnWndProc = WindowProc;
		wc.hInstance = NULL;
		wc.lpszClassName = CLASS_NAME;

		RegisterClass(&wc);

		hwnd = CreateWindowEx(
			0,
			CLASS_NAME,
			L"Websocket Controller",
			WS_OVERLAPPEDWINDOW,

			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

			HWND_MESSAGE,
			NULL,
			NULL, // ignore hInstance, cause this is console application
			NULL
			);
		server_Proc = (WNDPROC) SetWindowLong(hwnd, GWL_WNDPROC, (long)server_WndProc); // change from WindowProc to server_WndProc for handling messages
		if (!hwnd){
			cout << "Failed to create message window" << endl;
			system("pause");
			echo_server.stop();
			mySesh->join();
			session_stopped = true;
			return;
		}
		AddClipboardFormatListener(hwnd); // start listening for clipboard changes
		MSG msg = {};
		while (GetMessage(&msg, NULL, 0, 0)){
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		cout << "Server message window closed." << endl;
	};

	// message-getting loop for client
	auto lam = [](){
		// ** NOTE: GetMessage() has to be in same thread as where the window was created!!
		// otherwise, no messages will be gotten.
		const wchar_t CLASS_NAME[] = L"Sample Window Class";
		WNDCLASS wc = {};

		wc.lpfnWndProc = WindowProc;
		wc.hInstance = NULL;
		wc.lpszClassName = CLASS_NAME;

		RegisterClass(&wc);

		hwnd = CreateWindowEx(
			0,
			CLASS_NAME,
			L"Websocket Controller",
			WS_OVERLAPPEDWINDOW,

			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

			NULL,
			NULL,
			NULL, // ignore hInstance, cause this is console application
			NULL
			);
		if (!hwnd){
			cout << "failed to create window... exiting" << endl;
			c.stop();
			mySesh->join();
			return;
		}
		ShowWindow(hwnd, SW_RESTORE);
		getRectSize();
		g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
		MSG msg = {};
		while (GetMessage(&msg, NULL, 0, 0)){
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		cout << "Client window closed." << endl;
		// Since getline() won't end until it gets input... fake user input via istringstream
		// We're okay with doing this because user already expressed desire to close application
		// by closing the window.
		//Sleep(50);
	};

	auto fm = [](){
		RECT screen_size;
		if (!GetClientRect(hwnd, &screen_size)){
			screen_size.right = 800;
			screen_size.bottom = 600;
			cout << "Failed to get screen size" << endl;
			Sleep(100);
			int tries = 0;
			while (tries < 5 && !GetClientRect(hwnd, &screen_size)){
				cout << "failed to get screen size again... attempt " << (tries + 1) << " / 6" << endl;
				Sleep(100);
				tries++;
			}
			if (tries < 5){
				cout << "successfully got screen size on try: " << tries + 1 << endl;
			}
			else{
				cout << "Could not determine client's screen size; mouse functionality will not work properly." << endl;
			}
		}
		cout << "Client's screen size: X: " << screen_size.right << ", Y: " << screen_size.bottom << endl;
		float screen_max_x = (float)screen_size.right;
		float screen_max_y = (float)screen_size.bottom;
		POINT p;
		while (mouse_scanRun){
			// GetCursorPos(&p); will store mouse's coords in p, relative to screen
			// ScreenToClient(hwnd, &p); will do that relative to coords of a window... actually that just kept incrementing some number.. strange.
			if (hasFocus){
				if (GetCursorPos(&p)){
					p.x -= windowPos.left;
					p.y -= windowPos.top;
					if ((p.x >= 0 && p.y >= 0) && (p.x < windowPos.right && p.y < windowPos.bottom) && (lastmouse.x != p.x || lastmouse.y != p.y)){ // cursor inside window? && mouse pos different from last known pos
						// can put if(connected) here to prevent crashing if not connected
						string msg = "";
						msg += (char)2;

						// send local relative windows pre-mapped uint16 coords to server as 2 bytes for each x and y

						unsigned short x_uint16 = (short)(((float)p.x / screen_max_x) * 65535.0f);
						unsigned short y_uint16 = (short)(((float)p.y / screen_max_y) * 65535.0f);

						//msg = "";
						//msg += (char)2;
						msg += (char)x_uint16;
						msg += (char)(x_uint16 >> 8);
						msg += (char)y_uint16;
						msg += (char)(y_uint16 >> 8);

						//cout << "Relative mouse pos: X: " << (((float)p.x / screen_max_x) * 100.0f) << ", Y: " << (((float)p.y / screen_max_y) * 100.0f) << endl;
						//cout << "Char output: X: " << (int)(((float)p.x / screen_max_x) * 100.0f) << ", Y: " << (int)(((float)p.y / screen_max_y) * 100.0f) << endl;

						c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						lastmouse.x = p.x;
						lastmouse.y = p.y;

						/** DEBUG info for msg contents
						cout << "mouse moved" << endl;
						cout << "X: " << p.x << ", Y: " << p.y << endl;
						cout << "Message sent: ";
						for (int xint = 0, l = msg.length(); xint < l; xint++){
						cout << " [" << (int)msg[xint] << "] ";
						}
						cout << endl;
						**/
					}
				}
				Sleep(sleepTime); // sleep normal when window has focus
			}
			else{
				Sleep(idle_sleepTime); // less FPS when window doesn't have focus
			}
		}
	};

	if (clientMode){// registering window class
		scan = thread(lam);
		mouse_scan = thread(fm);
		/**
		MSG msg = {};
		while (GetMessage(&msg, NULL, 0, 0)){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		}
		**/
	}
	else{ // to receive and dispatch clipboard messages as the server
		//scan = thread(lam_server); // sigh, needs more work... starts off using WindowProc instead of server_WndProc... freezes on shutdown after window destroyed...

		// sometimes there is a problem with the keys being stuck... now a restart will fix that
		for (int xint = 0; xint < 255; xint++){
			if (hadKeys[xint])
				keybd_event(xint, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
		}
		mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
		mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
		mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
	}

	cout << "done now? " << done_starting << endl;

	if (session_start_success){
		if (clientMode){
			cout << "Client started successfully!" << endl;
			// change IME to english, so desktop doesn't have to switch manually to english.
			// (otherwise ALT+TILDE will toggle both laptop and desktop input type, and typing on desktop will cause text to go to the small window in the corner
			// and won't be forwarded as any kind of text properly

			// this doesn't actually seem to get the info i want xD when i check after having changed it, the values are the same... maybe it takes a few miliseconds to get the value.
			//TCHAR keyboard[KL_NAMELENGTH];
			//GetKeyboardLayoutName(keyboard);
			//WORD curlang = (WORD)keyboard;
			//cout << "starting IME: " << curlang << " (tchar: " << keyboard << ")" << endl;

			PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)LoadKeyboardLayoutA("00000409", KLF_ACTIVATE)); // change IME keyboard to english keyboard
			// actually that changed it to greece xD lol but it works the same i guess. just maybe unnecessary loading? oh. the place where i copied it from had it set to 408 instead of 409 >.>

		}
		else{
			cout << "Server started successfully!" << endl;
		}
		while (true){
			cout << "\nPlease enter command: ";
			string in = "";
			getline(cin, in);
			int inl = in.length();
			if (in != ""){
				cout << "ur in was : ('" << in << "')" << endl;
				if (in == "c"){
					string msg = "";
					msg += (char)5;
					msg += encodeHex(fromClipboard());
					if (clientMode){
						c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						cout << "Clipboard data sent to server!" << endl;
					}
					else{
						try{
							echo_server.send(connHdl, msg, websocketpp::frame::opcode::binary);
							cout << "Clipboard data sent to client!" << endl;
						}
						catch (...){
							cout << "Failed to send message containing clipboard text as server" << endl;
						}
					}
				}
				else if (in == "b"){
					if (clientMode){
						// window background color brush, to change background color
						//... white at night is too much light x.x
						// note: DC ('device context') is basic GDI drawing tools
						// like HBRUSH, HPEN, and HBITMAP as brushes, pens, and a canvas
						cout << "Background color set to black! NIGHTMODE activate!" << endl;
						cout << "'w' or minimize + restore to revert (glitch)" << endl;
						HDC hdc = GetWindowDC(hwnd);
						RECT rc; GetClientRect(hwnd, &rc);
						HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0)); // black
						FillRect(hdc, &rc, brush);
						DeleteObject(brush);
						ReleaseDC(hwnd, hdc);
						//SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, brush); // <-irrelevant?
					}
				}
				else if (in == "w"){
					if (clientMode){
						cout << "Background color set to white! DAYMODE activate!" << endl;
						HDC hdc = GetWindowDC(hwnd);
						RECT rc; GetClientRect(hwnd, &rc);
						HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255)); // black
						FillRect(hdc, &rc, brush);
						DeleteObject(brush);
						SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, WHITE_BRUSH);
					}
				}
				else if (in == "cbt"){
					if (clipboard_active){
						clipboard_active = false;
						cout << "Automatic clipboard transfer has been disabled." << endl;
					}
					else{
						clipboard_active = true;
						cout << "Automatic clipboard transfer has been enabled." << endl;
					}
				}
				else if (in == "cb_on"){
					clipboard_active = true;
					cout << "Automatic clipboard transfer has been enabled." << endl;
				}
				else if (in == "cb_off"){
					clipboard_active = false;
					cout << "Automatic clipboard transfer has been disabled." << endl;
				}
				else if (in == "bm"){ // benchmark, rapidly send over 5 messages from client to server and record times, then present data to user
					if (clientMode){
						std::string msg = "";
						msg += (char)6;
						msg += (char)1;
						msg += "123"; // message padding, msg needs to be 3 charrs
						cout << "starging benchmark..." << endl;
						for (int xint = 0; xint < 5; xint++){
							cout << "packet " << xint << " sent" << endl;
							c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						}
						cout << "benchmark complete" << endl;
					}
					else{
						cout << "Please use the client to start the benchmark" << endl;
					}
				}
				else if (in == "mo-cap"){
					if (clientMode){
						mo_cap_enabled = ~mo_cap_enabled;
						cout << "Mouse Capture toggled. Now set to: ";
						if (mo_cap_enabled){
							cout << "Enabled" << endl;
							SetCapture(hwnd);
							cout << "Now capturing mouse click input from outside client window." << endl;
							cout << "Disable by pressing ALT + F4 or by typing in 'mo-cap' again." << endl;
						}
						else{
							ReleaseCapture();
							cout << "Disabled" << endl;
						}
					}
					else{
						cout << "Mouse Capture is only for the client." << endl;
					}
				}
				else if (inl > 4 && in.substr(0, 4) == "fps "){
					int newfps = 0;
					try{
						newfps = stoi(in.substr(4));
					}
					catch (...){
						cout << "invalid number entered for fps" << endl;
					}
					if (newfps != 0){
						sleepTime = (1000 / newfps);
						cout << "Mouse-scanning FPS changed to: " << newfps << " (sleep time now: " << sleepTime << " )" << endl;
					}
				}
				else if (inl > 5 && in.substr(0, 5) == "ifps "){
					int newfps = 0;
					try{
						newfps = stoi(in.substr(5));
					}
					catch (...){
						cout << "invalid number entered for idle fps" << endl;
					}
					if (newfps != 0){
						idle_sleepTime = (1000 / newfps);
						cout << "Mouse-scanning FPS while idle changed to: " << newfps << " (idle sleep time now: " << idle_sleepTime << " )" << endl;
					}
				}
				else if (in == "q" || in == "quit" || in == "qs"){
					system("cls");
					cout << "quitting..." << endl;
					if (clientMode){
						if (in != "qs"){
							// send message for mutual quit so server shuts down too
							std::string msg = "";
							msg += (char)6;
							msg += (char)2;
							msg += (char)0; // unused, but msg must be > 2 chars long
							c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						}
						else{
							cout << "quit solo, server unaffected" << endl;
						}
						PostMessage(hwnd, WM_CLOSE, 0, NULL); // close window
						c.stop();
					}
					else{
						echo_server.stop();
					}
					sesh.join();
					if (clientMode){
						scanRun = false;
						scan.join();
						mouse_scanRun = false;
						mouse_scan.join();
					}
					else{
						// i just noticed that scanRun is not actually used anywhere anymore. huh. will rely on above postMessage WM_CLOSE instead to close message window of server
						//scan.join();
					}
					session_stopped = true;
					cout << "ok, done" << endl;
					break;
				}
				else if (inl > 1 && in[0] == 'm'){
					if (clientMode){
						c.send(connHdl, in, websocketpp::frame::opcode::binary);
					}
					else{
						try{
							// now thinking this won't work because the server doesn't have just 1 peer to connect
							echo_server.send(connHdl, in, websocketpp::frame::opcode::binary);
						}
						catch (...){
							cout << "Failed to send message" << endl;
						}
					}
					session_stopped = true;
				}
			}
		}
	}
	else{
		cout << "\nFailed to start." << endl;
		cout << "stopping assets..." << endl;
		sesh.join();
		scan.join();
		mouse_scan.join();
		session_stopped = true;
	}
	//cout << "bye bye" << endl;
	//system("pause");
}

// WindowProc to process the client's window messages
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
	if (wParam == SC_KEYMENU) return 0;
	// && (lParam >> 16) <= 0
	switch (uMsg){
	case WM_CLOSE:
		//if (MessageBox(hwnd, L"Really quit?", L"Close Window?", MB_OKCANCEL) == IDOK){
		DestroyWindow(hwnd);
		cout << "Please enter 'q' to quit" << endl;
		// also, since GetMessage() loop is done now, end getline() there via stringstream

		/**
		RESUME -- can maybe shut off by closing window by simulating keyboard input for console... like so:
		to terminate from child thread:
		SetForegroundWindow(GetConsoleWidnow());
		keybd_event(KEYBOARDEVENTF_KEYDOWN, 'p');
		Sleep(15);
		// p up
		Sleep(15);
		keybd_event(^, 'ENTER');
		// enter up
		**/


		return 0;
		//}
	case WM_DESTROY:
		RemoveClipboardFormatListener(hwnd);
		//i will use below instead to quit after window closes
		// it seems that WM_CLOSE may not be generated properly when clicking the X button on the window

		DWORD dwTmp;
		INPUT_RECORD ir[3];
		HANDLE hConIn;
		hConIn = GetStdHandle(STD_INPUT_HANDLE);

		ir[0].EventType = KEY_EVENT;
		ir[0].Event.KeyEvent.bKeyDown = TRUE;
		ir[0].Event.KeyEvent.dwControlKeyState = 0;
		ir[0].Event.KeyEvent.uChar.UnicodeChar = VK_RETURN;
		ir[0].Event.KeyEvent.wRepeatCount = 1;
		ir[0].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
		ir[0].Event.KeyEvent.wVirtualScanCode = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);

		ir[1].EventType = KEY_EVENT;
		ir[1].Event.KeyEvent.bKeyDown = TRUE;
		ir[1].Event.KeyEvent.dwControlKeyState = 0;
		ir[1].Event.KeyEvent.uChar.UnicodeChar = 'q';
		ir[1].Event.KeyEvent.wRepeatCount = 1;
		ir[1].Event.KeyEvent.wVirtualKeyCode = 'Q'; // might have to be UPPERCASE?
		ir[1].Event.KeyEvent.wVirtualScanCode = MapVirtualKey('Q', MAPVK_VK_TO_VSC);

		ir[2].EventType = KEY_EVENT;
		ir[2].Event.KeyEvent.bKeyDown = TRUE;
		ir[2].Event.KeyEvent.dwControlKeyState = 0;
		ir[2].Event.KeyEvent.uChar.UnicodeChar = VK_RETURN;
		ir[2].Event.KeyEvent.wRepeatCount = 1;
		ir[2].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
		ir[2].Event.KeyEvent.wVirtualScanCode = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);

		WriteConsoleInput(hConIn, ir, 3, &dwTmp);
		// that should have simmed RETURN, then q, then RETURN to clear the current cin input then enter q and newline to quit program
		PostQuitMessage(0);
		return 0;
	case WM_PAINT:
	{
					 PAINTSTRUCT ps;
					 HDC hdc = BeginPaint(hwnd, &ps);



					 FillRect(hdc, &ps.rcPaint, hbr_backgroundColor);

					 EndPaint(hwnd, &ps);
					 //cout << "Window refreshed" << endl;
					 break;
	}
	case WM_KEYDOWN:{
						//if (hadKeys[wParam])return 0; // this line prevents repeated messages while a key is held down
						//cout << "Received the key-up key code: " << wParam << endl;
						string msg = "";
						msg += (char)0;
						msg += (char)0;
						msg += (char)wParam;
						c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						hadKeys[wParam] = true;
						break;
	}
	case WM_KEYUP:{
					  //cout << "Received the key-up key code: " << wParam << endl;
					  string msg = "";
					  msg += (char)0;
					  msg += (char)1;
					  msg += (char)wParam;
					  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
					  hadKeys[wParam] = false;
					  break;
	}
	case WM_SYSKEYDOWN:{
						   if (hadKeys[wParam])return 0;
						   //cout << "Received sys key code: " << wParam << endl;
						   string msg = "";
						   msg += (char)1;
						   msg += (char)0;
						   msg += (char)wParam;
						   c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						   hadKeys[wParam] = true;
						   break;
	}
	case WM_SYSKEYUP:
	{
						//cout << "Received sys key code: " << wParam << endl;
						string msg = "";
						msg += (char)1;
						msg += (char)1;
						msg += (char)wParam;
						c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						hadKeys[wParam] = false;
						break;
	}
	case WM_LBUTTONDOWN:{
							//cout << "left click" << endl;
							string msg = "";
							msg += (char)3;
							msg += (char)0;
							msg += (char)0;
							c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							break;
	}
	case WM_LBUTTONUP:{
						  //cout << "left mouse up" << endl;
						  string msg = "";
						  msg += (char)3;
						  msg += (char)1;
						  msg += (char)0;
						  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						  break;
	}
	case WM_RBUTTONDOWN:{
							//cout << "right click" << endl;
							string msg = "";
							msg += (char)3;
							msg += (char)0;
							msg += (char)1;
							c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							break;
	}
	case WM_RBUTTONUP:{
						  //cout << "right mouse up" << endl;
						  string msg = "";
						  msg += (char)3;
						  msg += (char)1;
						  msg += (char)1;
						  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						  break;
	}
	case WM_MBUTTONDOWN:{
							//cout << "middle click" << endl;
							string msg = "";
							msg += (char)3;
							msg += (char)0;
							msg += (char)2;
							c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							break;
	}
	case WM_MBUTTONUP:{
						  //cout << "middle mouse up" << endl;
						  string msg = "";
						  msg += (char)3;
						  msg += (char)1;
						  msg += (char)2;
						  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						  break;
	}
	case WM_MOUSEWHEEL:{
						   //MSLLHOOKSTRUCT *info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam); // cast lParam into Low Level Mouse Hook
						   signed int x = (signed int)(HIWORD(wParam)); // scrolled down ? (down = toward user... i think)
						   //cout << "wParam: " << wParam << "\n HIWORD: " << HIWORD(wParam) << "\nsint32: " << x << endl;
						   bool down = false;
						   if (x < 32768){ // up is a multiple of 120 (120 per roll amount, but rarely more than 1 so usually 1 120)
							   //cout << "mouse scrolled up" << endl;
						   }
						   else{
							   down = true;
							   //cout << "mouse scrolled down" << endl;
							   x -= 65296;
						   }
						   string msg = "";
						   msg += (char)4;
						   msg += (down ? ((char)0) : ((char)1));
						   msg += (char)(x / 120);
						   c.send(connHdl, msg, websocketpp::frame::opcode::binary);
						   break;
	}
	case WM_ACTIVATE:{
						 // delete folloinwg debug output
						 /**
						 cout << "wParam: " << wParam << endl;
						 switch (wParam){
						 case WA_INACTIVE:{
											  cout << "case: INACTIVE" << endl;
											  break;
						 }
						 case WA_CLICKACTIVE:{
												 cout << "case: CLICK ALIVE" << endl;
												 break;
						 }
						 case WA_ACTIVE:{
											cout << "active" << endl;
											break;
						 }
						 default:{
									 cout << "found nu'in :/" << endl;
						 }
						 }
						 **/
						 if (wParam == WA_INACTIVE){
							 //cout << "Window lost focus!" << endl;
							 hasFocus = false;
						 }
						 else if(wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE){
							 //cout << "Window lost focus -- WM_ACTIVATE" << endl;
							 hasFocus = true;
						 }
						 break;
	}
	case WM_KILLFOCUS:{
						  //cout << "Window lost focus -- WM_KILLFOCUS" << endl;
						  hasFocus = false;
						  break;
	}
	case WM_SYSCOMMAND:{
						   if (wParam == SC_MINIMIZE){
							   isMinimized = true;
						   }
						   else if (wParam == SC_RESTORE){
							   isMinimized = false;
						   }
						   break;
	}
/** // apparently i checked for minimzed before, guess i just went with focus events
	case WM_SIZE:{
					 // window has changed size
					 cout << "WM_SIZE" << endl;
					 if (wParam == SIZE_MINIMIZED){
						 hasFocus = false;
					 }
					 break;
	}
	case WM_SYSCOMMAND:{
						   // received system command
						   cout << "SYS COMMAND" << endl;
						   if (wParam == SC_MINIMIZE){
							   hasFocus = false;
							   justMinimized = true;
						   }
						   break;
	}
	**/
	case WM_MOVE:{
					 //cout << "Window moved" << endl;
					 getRectSize();
					 break;
	}
	case WM_CLIPBOARDUPDATE:{
								if (!clipboard_active || hasFocus) // return DefWindowProc(hwnd, uMsg, wParam, lParam);
									return 0;
								if (!server_to_client_clipboard && OpenClipboard(nullptr)){
									HANDLE hData = GetClipboardData(CF_TEXT); // CF_UNICODETEXT will only put 1 char into the char *pszText, CF_TEXT will work normally
									if (hData != nullptr){
										char *pszText = static_cast<char*>(GlobalLock(hData));
										//cout << "pszText = " << pszText << endl;
										if (pszText != nullptr){
											string clipboard_text(pszText);
											string msg = "";
											msg += (char)5;
											msg += clipboard_text;

											GlobalUnlock(hData);
											CloseClipboard();

											int textmsg = 0;
											// could check that all chars in first 10 bytes of string are ascii... <= 'z' >= '1'
											// we already know byte 0 will be 5, so just do next 10
											string trace = "5";
											for (int xint = 1, l = (msg.length() > 10 ? (11) : (msg.length())); xint < l; xint++){
												if ((msg[xint] >= 'A' && msg[xint] <= 'Z') || (msg[xint] >= 'a' && msg[xint] <= 'z')){
													textmsg++;
													//break;
												}
												else{
													textmsg--;
												}
													trace += " , ";
													trace += to_string((int)msg[xint]);
													trace += "(";
													trace += msg[xint];
													trace += ")";
											}
											if (textmsg){
												c.send(connHdl, msg, websocketpp::frame::opcode::text);
												cout << "clipboard text sent to server: " << msg << endl;
											}
											cout << "text message was " << textmsg << endl;
											cout << "now the useful stuff: " << trace << endl;
										}
									}
								}
								// w8 w8 w8, was this line below the problem?? maybe it was changing the clipboard because of some reciporical reception problem
								// or something...
								return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
	case WM_CREATE:{
					   AddClipboardFormatListener(hwnd);
					   break;
	}
		return 0;
	}
	// maybe should use following since i'm using a separate WNDPROC for server ( & rename this function to client_WndProc):
	// client_Proc = SetWindowLong(hwnd, GWL_WNDPROC, (long) client_WndProc);
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(INT nCode, WPARAM wParam, LPARAM lParam){
	if (!hasFocus){
		if (nCode == HC_ACTION && ((KBDLLHOOKSTRUCT*)lParam)->vkCode == VK_RWIN){
			// Right Windows key is a shortcut to open up the client window
			if (wParam == WM_KEYUP){
				if (isMinimized){
					//ShowWindow(hwnd, SW_RESTORE); // needs forceShowWindow
					forceShowWindow(hwnd, SW_RESTORE);
					isMinimized = false;
				}
				else{
					forceShowWindow(hwnd, SW_NORMAL);
				}
			}
			return 1; // block key up and down, restore on key up only
		}
		return 0; // only check the keys and take over IF window has focus. bit of an inconvenience otherwise >.>
	}

	// By returning a non-zero value from the hook procedure, the
	// message does not get passed to the target window
	KBDLLHOOKSTRUCT *pkbhs = (KBDLLHOOKSTRUCT *)lParam;
	BOOL bControlKeyDown = 0;

	switch (nCode)
	{
	case HC_ACTION:
	{
					  // Check to see if the CTRL key is pressed
					  bControlKeyDown = GetAsyncKeyState(VK_CONTROL) >> ((sizeof(SHORT)* 8) - 1);

					  // ok, finally replaced all the if(hadKeys[]) with if(wParam == KEYDOWN/KEYUP)... so i shouldn't have a problem with sticky keys anymore ^_^ hopefully
					  // UPDATE 15 min l8r: WM_KEYDOWN has a value of 256, but when a key is pressed, wParam becomes 260, and 261 on release... so i need to use those numbers.
					  // AHA! I stand corrected!! Only for ALT is it 260/261... for Windows Key & Control (etc, i guess) it's 256 as the normal WM_KEYDOWN
					  // tested and working... which means... I'M FINALLY STICKY FREEEEE!!! WOOOHOOOO STICKY-FREE IS THE LIFE FOR ME!!!!

					  // Disable CTRL+ESC
					  if (pkbhs->vkCode == VK_ESCAPE && bControlKeyDown){
						  //cout << "KEYDOWN = " << WM_KEYDOWN << " and wParam is " << wParam << " is key down? : ";
						  //if (!hadKeys[VK_ESCAPE]){
						  if (wParam == WM_KEYDOWN){
							  //cout << "down";
							  string msg = "";
							  msg += (char)0;
							  msg += (char)0;
							  msg += VK_ESCAPE;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_ESCAPE] = true;
						  }
						  else if (wParam == WM_KEYUP){
							  //cout << "up";
							  string msg = "";
							  msg += (char)0;
							  msg += (char)1;
							  msg += VK_ESCAPE;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_ESCAPE] = false;
						  }
						  //cout << endl;
						  return 1;
					  }

					  /** didn't work...
						if (GetKeyState(VK_LSHIFT)){
							if (!hadKeys[VK_LSHIFT]){
								hadKeys[VK_LSHIFT] = true;
							}
						}
						else{
							if (hadKeys[VK_LSHIFT]){
								hadKeys[VK_LSHIFT] = false;
							}
						}*/

					  // Disable ALT+TAB
					  if (pkbhs->vkCode == VK_TAB && pkbhs->flags & LLKHF_ALTDOWN){
						  // NOTE TO FUTURE SELF: replace tab sim with a message sent to server to sim either Alt+Tab or Alt+Shift+Tab, cause shift is not detected this way
						  // well i used wParam == 260/261 & now LShift is detected with it ^_^ !!!
						  //if (!hadKeys[VK_TAB]){
						  //cout << "KEYDOWN = " << WM_KEYDOWN << " and wParam is " << wParam << " is key down? : ";
						  if(wParam == 260){
							  //cout << "down";
							  string msg = "";
							  /** didn't work as should... only detected shift press/release when alt is not pressed, shift is sticky and needs to be  held down because of multiple messages or something...
							  if (hadKeys[VK_LSHIFT]){
								  msg += (char)6;
								  msg += (char)0;
								  msg += (char)1; // backwards
							  }
							  else{
								  msg += (char)6; // send special key/macro message
								  msg += (char)0; // P.S. this message is sent twice, could set hadKeys to true then toggle
								  msg += (char)0; // normal, forward tab
							  }
							  **/
							  msg += (char)0; // message for normal ALT+Tab, but it doesn't register Shift being pressed... so only goes one direction. :/
							  msg += (char)0;
							  msg += VK_TAB;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_TAB] = true;
						  }
						  else if (wParam == 261){
							  //cout << "up";
							  string msg = "";
							  msg += (char)0;
							  msg += (char)1;
							  msg += VK_TAB;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_TAB] = false;
						  }
						  //else{
							 // cout << "neither!!";
						  //}

						  /*if (!hadKeys[VK_TAB]){
							  hadKeys[VK_TAB] = true;
							  if (hadKeys[VK_SHIFT]){
							  }
							  else{

							  }
						  }
						  else{
							  hadKeys[VK_TAB] = false;
						  }
						  */
						  //cout << endl;
						  return 1;
					  }


					  // Disable ALT+SHIFT (because it will scroll through my IMEs
					  // just blocking / forwarding Left Shift
					  if (pkbhs->vkCode == VK_LSHIFT && pkbhs->flags & LLKHF_ALTDOWN){
						  //if (hadKeys[VK_LSHIFT]){
						  if(wParam == 260){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)0;
							  msg += VK_LSHIFT;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_LSHIFT] = true;
						  }
						  else if(wParam == 261){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)1;
							  msg += VK_LSHIFT;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_LSHIFT] = false;
						  }
						  return 1;
					  }

					  if (pkbhs->vkCode == 0x56){ // V key, for CTRL+SHIFT+V pasting
						  BOOL bShiftKeyDown = GetAsyncKeyState(VK_SHIFT) >> ((sizeof(SHORT)* 8) - 1);
						  if (bShiftKeyDown && bControlKeyDown){
							  if (wParam == WM_KEYUP){
								  // send client clipboard to server
								  string msg = "";
								  msg += (char)5;
								  msg += encodeHex(fromClipboard());
								  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
								  //cout << "Clipboard data sent to server!" << endl;
							  }
							  return 1; // don't forward for V up/down with CTRL+SHIFT, avoid sticky keys
						  }
					  }
					  
					  if (pkbhs->vkCode == 0x43){ // C key, for CTRL+SHIFT+C getting server clipboard
						  BOOL bShiftKeyDown = GetAsyncKeyState(VK_SHIFT) >> ((sizeof(SHORT)* 8) - 1);
						  if (bShiftKeyDown && bControlKeyDown){
							  if (wParam == WM_KEYUP){
								  // get server clipboard sent to client
								  string msg = "";
								  msg += (char)6;
								  msg += (char)3;
								  msg += (char)0; // need 3rd byte so server receives message. Otherwise it will think it's too small
								  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
								  //cout << "Clipboard data requested from server!" << endl;
							  }
							  return 1;
						  }
					  }


					  // Disable ALT+ESC
					  if (pkbhs->vkCode == VK_ESCAPE && pkbhs->flags & LLKHF_ALTDOWN){
						  //if (!hadKeys[VK_ESCAPE]){
						  if(wParam == 260){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)0;
							  msg += VK_ESCAPE;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_ESCAPE] = true;
						  }
						  else if(wParam == 261){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)1;
							  msg += VK_ESCAPE;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_ESCAPE] = false;
						  }
						  return 1;
					  }

					  // Disable Alt+F4
					  if (pkbhs->vkCode == VK_F4 && pkbhs->flags & LLKHF_ALTDOWN){
						  cout << "Mouse Capture Released (by ALT + F4)" << endl;
						  mo_cap_enabled = false;
						  //if (!hadKeys[VK_F4]){
						  if(wParam == 260){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)0;
							  msg += VK_F4;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_F4] = true;
						  }
						  else if(wParam == 261){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)1;
							  msg += VK_F4;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_F4] = false;
						  }
						  return 1;
					  }
					  if (pkbhs->vkCode == VK_LWIN){ // block & send windows key -- only Left WinKey
						  //if (!hadKeys[VK_LWIN]){
						  if (wParam == WM_KEYDOWN){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)0;
							  msg += VK_LWIN;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_LWIN] = true;
						  }
						  else if(wParam == WM_KEYUP){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)1;
							  msg += VK_LWIN;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_LWIN] = false;
						  }
						  return 1;
					  }

					  if (pkbhs->vkCode == VK_RWIN){ // block Right Windows Key. Minimize client window.
						  if (wParam == WM_KEYUP){
							  ShowWindow(hwnd, SW_MINIMIZE);
						  }
						  return 1; // block key up & key down, only minimize on key up
					  }

					  // PRINT SCREEN Key -- forward it to server
					  if (pkbhs->vkCode == VK_SNAPSHOT){
						  //if (!hadKeys[VK_LWIN]){
						  if (wParam == WM_KEYDOWN){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)0;
							  msg += VK_SNAPSHOT;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_LWIN] = true;
						  }
						  else if (wParam == WM_KEYUP){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)1;
							  msg += VK_SNAPSHOT;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_SNAPSHOT] = false;
						  }
						  return 1;
					  }


					  // disable CTRL  + Caps Lock && Alt + Caps lock for japanese IME switching to hiragana/katakana
					  if (pkbhs->vkCode == VK_CAPITAL && pkbhs->flags & LLKHF_ALTDOWN){
						  if (wParam == 260){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)0;
							  msg += VK_CAPITAL;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_CAPITAL] = true;
						  }
						  else if (wParam == 261){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)1;
							  msg += VK_CAPITAL;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_CAPITAL] = true;
						  }

						  return 1;
					  }
					  else if (pkbhs->vkCode == VK_CAPITAL && bControlKeyDown){
						  if (wParam == WM_KEYDOWN){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)0;
							  msg += VK_CAPITAL;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_CAPITAL] = true;
						  }
						  else if (wParam == WM_KEYUP){
							  string msg = "";
							  msg += (char)0;
							  msg += (char)1;
							  msg += VK_CAPITAL;
							  c.send(connHdl, msg, websocketpp::frame::opcode::binary);
							  hadKeys[VK_CAPITAL] = true;
						  }

						  return 1;
					  }

					  break;
	}

	default:
		break;
	}
	return CallNextHookEx(mule, nCode, wParam, lParam);
}
void getRectSize(){
	// GetWindowRect() gets window position
	// GetClientRect() gets width and height of client window
	RECT wnd;
	GetWindowRect(hwnd, &wnd);
	RECT client;
	GetClientRect(hwnd, &client);
	// to get client rect from top left, subtract length of client from length of parent window to get border size
	// then add x,y pos to the width,height of border size
	windowPos.left = wnd.left + ((wnd.right - client.right) - wnd.left);
	windowPos.top = wnd.top + ((wnd.bottom - client.bottom) - wnd.top);
	windowPos.left -= 3; // a slight fix to put the cursor at the extreme upperleft to show 0,0
	windowPos.top -= 5;
	windowPos.right = client.right;
	windowPos.bottom = client.bottom;
}

// windowProc for server (register with SetWindowLong(hwnd, GWL_WINDPROC, (long)server_WndProc)-- https://stackoverflow.com/questions/7538883/multiple-wndproc-functions-in-win32
LRESULT CALLBACK server_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
	switch (uMsg){
	case WM_CLIPBOARDUPDATE:{
								if (!server_to_client_clipboard && OpenClipboard(nullptr)){
									HANDLE hData = GetClipboardData(CF_TEXT);
									if (hData != nullptr){
										char *pszText = static_cast<char*>(GlobalLock(hData));
										if (pszText != nullptr){
											string clipboard_text(pszText);
											string msg = "";
											msg += (char)5;
											msg += clipboard_text;

											GlobalUnlock(hData);
											CloseClipboard();

											//c.send(connHdl, msg, websocketpp::frame::opcode::binary);
											try {
												echo_server.send(connHdl, msg, websocketpp::frame::opcode::binary);
											}
											catch (websocketpp::exception const & e) {
												std::cout << "Echo failed because: "
													<< "(" << e.what() << ")" << std::endl;
											}
											cout << "clipboard text sent to client: " << msg << endl;
										}
									}
								}
								break;
	}
	case WM_CREATE:{
					   cout << "WINDOW CREATED SUCCESSFULLY" << endl;
		break;
	}
	case WM_DESTROY:{
		RemoveClipboardFormatListener(hwnd);
		break;
	}
	default:
		return CallWindowProc(server_Proc, hwnd, uMsg, wParam, lParam);
	}
}