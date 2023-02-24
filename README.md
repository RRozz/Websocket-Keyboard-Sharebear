Titled "Websocket Controller", it is a C++ program that acts as proof of concept for forwarding input given to the program's window to another computer.

Built with small messages in mind to keep CPU usage minimal, so it can be used while multi-tasking for long periods. Mouse position forwarding is done by scanning by a second thread (mouse movement would cause a ton of events), while mouse clicks/scrolls and keyboard are event-based.

The one program is used for both client & server (determined at startup). Choose to be client or server through console, or start the program with arguments specifying its role.

# It can:
<ul>
<li>Forward keyboard strokes (window must have focus)</li>
<li>Forward mouse movement and clicks/scrolls (window must have focus)</li>
<li>Conduct a primitive latency benchmark (enter 'bm' in client console, results are in server client)</li>
<li>Send ASCII in the clipboard of client the to the clipboard of the server (enter 'c' in client console)</li>
</ul>

# Negated and forwarded keys/combos
<b>While the client window has focus, the client will forward these keys to sim on the server without letting the keys' functions play out on the host.<br>
Print Screen</b><br>
Left Windows Key (Right Windows Key will always both forward to server & open menu on client which results in sticky key on server until it is pressed again for the server)<br>
Hold Alt and press Shift (to scroll through IMEs) (holding Shift and pressing Alt will both forward to server & enact on client)<br>
CTRL + CapsLock / Alt + CapsLock (for Japanese IME)<br>
CTRL + Escape<br>
Alt + Escape<br>
Alt + F4<br>

# Limitations
<ul>
<li>While it does forward input well and (for typing) reliably, it cannot be used to play action games because of 'resevoir latency' (see below). Because of this, rapidly pressing right,left,right,left,right,left (etc) will only result in one right. Or one left. Per half second. It does no harm but doesn't send over / properly simulate all key presses.</li>
<li>The mouse update speed is also affected by this 'resevoir latency' effect. The max FPS the mouse would update on the server's side is about 5 FPS. Regardless of the mouse scan speed setting, latency found in benchmarking, or connection method (I've always kept it ethernet-tethered), it's always 5 FPS, which leads me to believe it's a network protocol/library thing.</li>
<li>'Resevoir latency' is what I'm referring to as only sending input every once in a while, but combining messages accumulated up to that period. When you type: it forwards it every 0.25 seconds or so, so you will notice not only a hint of delay, but if you type quickly, multiple letters will appear at once. I believe this is why the mouse is slow, too. Because it accumulates multiple 'go here' messages for the mouse and executes them in such a small timeframe that only the last frame has meaningful impact. I think it's a thing with my networking protocol/library of choice. Didn't know it when I chose it, but I only use it for typing.</li>
<li>Also noteworthy is permissions: Playing a game running as administrator? Websocket Controller needs to be running as administrator. Otherwise the administrator-privileged window with focus will completely ignore the input, and you couldn't change the window without a physical device. I think the "Run program as administrator?" prompt requires system privileges (as TeamViewer runs as System and can sim controls during that period). To run as system would require either registering and starting the Websocket Controller as a service (I read there's software to convert normal PEs into services, didn't look into it yet), or you would have to have Websocket Controller inherit permissions from another program/service that's already running as system.</li>
</ul>


# Who would most benefit from it
<ol>
<li>Someone that wants to type and play non-actioney/interface-based games would get the most fulfillment.</li>
<li>Someone who has a desktop to their side and a laptop in front of them, and wants to control both with one keyboard would get the most convenience.</li>
<li>Someone who wants to control a computer with a large screen on the other side of the room from a small laptop in the corner would find it useful as well.</li>
</ol>


# Testing environment
Tested on 2 Windows 7 computers, built as 32-bit<br>
Server: 1366x768, Client: 1920x1080<br>
Worked both over ethernet and wifi with the same minimal latency.<br>
I've used this setup daily since its creation a couple years ago.<br>
I've read that the references to mouse_event() (and maybe keybd_event()) would need to be changed to SendInput() for use with Windows 10.

P.S. source code is littered with comments of notes to myself / debug residue, please don't mind. :)
