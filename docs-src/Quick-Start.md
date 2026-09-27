% C64uRemote for M5Dial
% Quick start – up and running in ten minutes
% Firmware 1.3.1

# What you need

- the M5Dial with the microSD card inserted, and the NFC cards from the package
- a **Commodore 64 Ultimate (c64u)** or Ultimate64 Elite-II
- your Wi-Fi: name and password. The M5Dial uses the **2.4 GHz band only** – it
  cannot see a 5 GHz-only network.
- a phone or laptop with a browser for the setup

The M5Dial and the c64u must be on the **same network**. A guest network usually
does not work, because the router keeps the devices in it apart.

# Step 1: Get the c64u onto the network

You do this once on the C64 itself, in the **Ultimate menu**. Open it with the
menu button on the machine or with **C= and RESTORE** together.

1. **Connect the network.**
   *Via Wi-Fi:* open **Wi-Fi Network Setup**, show the list of networks, choose
   yours and enter the password.
   *Via cable:* plug in a network cable and open **Wired Network Setup**.
2. **Write down the IP address.** Once connected, the same menu shows the active
   IP address, for example `192.168.178.47`. **You need this address in step 3.**
3. **Enable remote control.** Under **Network Services & Timezone** set
   **Web Remote Control Service** to **Enabled**. Without this service the M5Dial
   cannot reach the c64u.
4. **Network password (optional).** If you have set a *Network Password* on the
   c64u, write it down too. Without a password, leave that field empty in step 3.

Depending on the c64u firmware, menu items may be named slightly differently – if
in doubt, see chapter 11 ("Networking and Wi-Fi") of the c64u user's guide.

> **Tip: fixed address in the router.** So that the c64u does not get a new
> address one day and the M5Dial can no longer find it, reserve the address in
> your router (often called *DHCP reservation* or *always assign the same IP
> address*).

# Step 2: Switch on the M5Dial

Plug in the USB-C cable or – with a battery – press the dial ring. On first start
the device shows **SETTINGS > WIFI**: it does not know a network yet. The small
dot at the very top of the display is red.

Quick controls: **turn** moves the selection, **press** the ring to select,
**press long** to go back one level.

# Step 3: Set up the M5Dial (setup portal)

1. Turn the ring – the ring menu opens. Select **Settings**, then keep
   turning to **WiFi** and press.
2. Select **Setup portal** and press. The display now shows:

   | | |
   |---|---|
   | Network name | `C64uRemote-Setup` |
   | Password | `c64ultimate` |
   | Address  | `192.168.4.1` |

3. Connect your phone or laptop to this network **C64uRemote-Setup**. The setup
   page usually opens by itself; otherwise open **http://192.168.4.1** in the
   browser.
4. Fill in the page:
   - **Networks found:** choose your Wi-Fi (or type the name by hand)
   - **WiFi password:** the password of your Wi-Fi
   - **c64u address:** the IP address from step 1, e.g. `192.168.178.47`
   - **c64u password:** only if you have set one on the c64u
5. Tap **Save and connect**. The M5Dial closes the setup network and connects to
   your Wi-Fi. The phone losing its connection is normal – it returns to your own
   Wi-Fi by itself.

The setup portal switches itself off after five minutes without use. Just start it
again.

# Step 4: Check

After a few seconds the dot at the top of the display shows the connection:

| Dot | Meaning | What to do |
|---|---|---|
| **green** | everything connected | done! |
| **blue** | Wi-Fi is up, the c64u does not answer | c64u on? Address correct? *Web Remote Control Service* enabled? |
| **yellow** | c64u reachable, wrong password | correct the c64u password in the setup portal |
| **red** | no Wi-Fi | check the Wi-Fi password, is 2.4 GHz switched on? |

In the ring menu the **Status (i)** icon shows the same colour. The *Status* page
has all details: Wi-Fi, IP address, configured c64u address, reachability – and
the firmware version at the very bottom.

# Start playing

Lay an NFC card flat on the M5Dial while the home screen or the ring menu is
shown. The game is transferred from the microSD card to the c64u and starts by
itself. The command cards trigger reset, Ultimate menu and so on.

Everything else – writing your own cards, settings, switching off, battery – is in
the full **User Manual**.

# Other ways onto the Wi-Fi

Instead of the setup portal you can also:

- **File on the microSD:** put a text file `wifi.txt` into the root of the card
  and restart the M5Dial (or *Settings → WiFi → Load from SD*):

  ```
  ssid     = MyWiFi
  pass     = MyWiFiPassword
  host     = 192.168.178.47
  hostpass =
  ```

- **NFC card:** with a phone app such as *NFC Tools*, write the text
  `WIFI:S:MyWiFi;T:WPA;P:MyPassword;;` onto a blank card and lay it on the device.
  The c64u address still has to be entered via the setup portal or `wifi.txt`.

# Without a router: direct mode

At a meeting without WiFi the M5Dial opens a network of its own:
*Settings → WiFi → Direct mode*. On the c64u, enter the WiFi
`C64uRemote-Direct` with the password `c64ultimate` once; it then gets the
address `192.168.4.64`. The details are in the **User Manual**, chapter
*Direct mode*.
