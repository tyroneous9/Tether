# Knob Follower

Turn a knob, the servo turns to match: a position follower that maps the knob's angle onto the servo's angle.

## Contents

- [Architecture](#architecture)
- [Hardware](#hardware)
- [Breadboard layout](#breadboard-layout)
- [Software](#software)
- [Repository layout](#repository-layout)
- [Build order](#build-order)
- [How it fits together](#how-it-fits-together)
- [Documentation](#documentation)

## Architecture

- **STM32 Nucleo** handles everything hard-real-time: sampling the potentiometer with the ADC and generating the servo's PWM signal. Bare-metal or FreeRTOS.
- **Raspberry Pi 5** is the embedded Linux target. It runs a **custom-built minimal image**, not stock Raspberry Pi OS, and talks to the Nucleo over serial through a **custom kernel driver**, not a raw Python `pyserial` script.

The Nucleo/Pi split keeps Linux out of anything latency-sensitive while giving the Linux side real substance to showcase.

```mermaid
flowchart LR
  P[Potentiometer] -- analog voltage --> N[STM32 Nucleo<br/>ADC]
  P -. side exercise .-> X[ADS7830<br/>I2C ADC] -. I2C .-> N
  N -- PWM --> S[SG90 servo]
  N -- UART --> K[Kernel driver<br/>on Pi 5]
  K -- sysfs --> A[User-space daemon]
```

The knob-to-servo loop runs entirely on the Nucleo; the Pi only observes and reports position.

## Hardware

| Part | Notes |
| --- | --- |
| STM32 Nucleo board | Nucleo-F446RE. F4-series Cortex-M4 rated up to 180 MHz (this project clocks it at 84 MHz from the internal HSI through the PLL; all timer math below assumes that), with three 12-bit ADCs for reading the pot, I2C for the external ADC, and plenty of hardware timers for triggering ADC samples and generating the servo's PWM |
| Potentiometer | B10K: 10 kΩ, linear taper ("B"). Passive part, no firmware of its own: it's a voltage divider. Outer legs to 3.3 V and GND (swapping them only reverses direction), wiper (middle leg) to an ADC-capable pin: PA0, labelled A0 on the Nucleo. Power it from 3.3 V, not 5 V: the ADC input must stay between 0 V and VDDA (3.3 V). An optional 100 nF capacitor from wiper to GND cuts noise. Unsure which leg is which? The pair reading a constant 10 kΩ on a multimeter are the outer legs |
| External ADC (side exercise) | ADS7830 (Texas Instruments): 8-bit, 8-channel, I2C. Address 0x48–0x4B depending on its A0/A1 pins (fixed on a breakout module; scan to find it). Read by writing one command byte (channel + power-down/reference bits) and reading back one byte (0–255). Two datasheet gotchas: the channel-select bits aren't in plain binary order, and the internal reference gives a 2.5 V full scale, so a 3.3 V pot would clip at 255 for the last part of its turn. Choose the reference setting that matches how the module is wired. Powered from 3.3 V. Used to learn I2C, not in the main loop |
| Servo | Blue micro servo, unlabelled: an SG90-class hobby servo. Standard hobby-servo interface: a pulse every 20 ms (50 Hz), pulse width sets the angle, about 1 ms at one end, 1.5 ms centre, 2 ms at the other end (many SG90s go further, roughly 0.5–2.5 ms; find the real limits by testing, and back off if it buzzes or strains at an end). About 180° of travel. Wires: red is power (middle), the darkest is GND, the lightest (orange/yellow) is signal. Confirm which is which before powering it (see [Breadboard layout](#breadboard-layout)) |
| Raspberry Pi 5 | Target for the custom Linux image |
| Link | USB first: USART2 is wired to the ST-Link virtual COM port, which appears as `/dev/ttyACM0` on Linux. Later, a second UART (USART1) wired to the Pi's GPIO header. USART2 can't be used for the header link because its pins (PA2/PA3) are tied to the ST-Link by default |
| Breadboard | Full-size (830-point) solderless breadboard. The Nucleo and Pi sit beside it; the pot, ADS7830 module, servo connection and power wiring go on it |
| Jumper wires | Male-to-male for the Nucleo's Arduino headers (female sockets) and the servo's 3-pin female connector; female-to-male for the Nucleo's morpho pins and the Pi's GPIO header (male pins) |
| Servo power supply | Separate 5 V supply, 1 A or more: a USB breadboard power module, a 5 V wall adapter through a barrel-jack breadboard adapter, or 4×AA (about 6 V, within the SG90's range). Never power the servo from the Nucleo or the Pi: its current spikes, hundreds of mA when it stalls, can brown out and reset the board |
| Capacitor | 100–470 µF electrolytic, rated 10 V or more, across the servo supply rail near the servo. Absorbs the servo's current spikes so they don't disturb the rest of the circuit |
| Debug aids | Serial console for the Pi, to see the boot log independent of SSH/network. The Pi 5's debug UART is a 3-pin JST-SH connector, so use the Raspberry Pi Debug Probe or a USB-to-serial adapter with a JST-SH 3-pin cable; a plain jumper-wire USB-serial cable won't plug in; a multimeter to confirm the wiper sweeps smoothly from 0 V to 3.3 V; optionally a cheap logic analyzer to measure the servo's pulse widths and decode the ADS7830's I2C traffic |

### Breadboard layout

**Nucleo pin assignments.** One place to check before assigning a new pin in CubeMX:

| Function | MCU pin | Nucleo header | Peripheral |
| --- | --- | --- | --- |
| Pot wiper (ADC input) | PA0 | A0 | ADC1 channel 0 |
| Servo signal (PWM) | PA6 | D12 | TIM3 channel 1 |
| Debug serial to PC | PA2 / PA3 | (ST-Link, not on the header) | USART2 |
| Serial link to Pi | PA9 (TX) / PA10 (RX) | D8 / D2 | USART1 |
| ADS7830 SCL / SDA | PB8 / PB9 | D15 / D14 | I2C1 |
| User LED | PA5 | D13 | GPIO (LD2) |
| User button | PC13 | (on board) | EXTI |
| ADC sample trigger | (no pin) | | TIM2 TRGO |

- **Two power rails, kept separate.** One rail carries 3.3 V from the Nucleo's 3V3 pin for logic: the pot and the ADS7830. The other carries the 5 V servo supply. Label them. On some full-size breadboards the rails are split halfway along, so check continuity with a multimeter before you trust them.
- **One shared ground.** Nucleo GND, Pi GND, the servo supply's GND and the servo's GND wire all go to the same ground rail. Without a common ground, the PWM and UART signals have no reference voltage and won't work reliably.
- **Servo pin identification.** Hobby servo connectors always put red (power) in the middle; the two outer wires are GND and signal. GND is the darker one (normally brown or black), signal the lighter one (orange, yellow or white). If the two outer wires look alike, compare them side by side in good light, or look up a photo of an SG90 connector. Getting GND and signal swapped is usually survivable at 5 V, but don't leave it powered that way.
- **Servo signal level.** The servo is powered from 5 V but driven by the Nucleo's 3.3 V PWM pin. SG90s accept a 3.3 V signal, so no level shifter is needed. The signal wire goes to the Nucleo only; never connect it to the 5 V rail.
- **ADS7830 module.** Connect SCL to D15 (PB8) and SDA to D14 (PB9) on the Nucleo's Arduino header, which are I2C1. I2C needs pull-up resistors on both lines; modules usually include them, but if yours doesn't, add about 4.7 kΩ from each line to 3.3 V. For the side-by-side comparison, run a jumper from the pot's wiper to one of the ADS7830's input channels as well as to PA0.
- **Respect breadboard current limits.** Breadboard contacts are only good for roughly 1 A. A single SG90 stays under that, even stalled.
- **Keep jumpers short and check them first.** A loose jumper is the most common cause of a circuit that worked yesterday and doesn't today. Before you suspect firmware, push each wire in firmly and check continuity. These bugs make good material for the debugging writeup.

## Software

### STM32 side

- ADC reads the wiper voltage, triggered by a hardware timer at a fixed rate (e.g. 1 kHz) with results moved by DMA, so sampling is evenly spaced and the CPU never busy-waits on a conversion
- Filter the raw 12-bit reading (moving average or a simple IIR low-pass) and apply a small deadband, so ADC noise of a few counts doesn't make the servo jitter
- Map the filtered value linearly onto the servo's pulse-width range (e.g. 1000–2000 µs to start, widened once the real limits are known). The input range is nominally 0–4095 (12-bit ADC), but use the pot's measured end readings, since a real pot rarely reaches exactly 0 and 4095
- Generate the servo signal with TIM3 channel 1 in PWM mode 1 on PA6 (D12 on the Nucleo's Arduino header). With the 84 MHz timer clock, prescaler 83 gives 1 MHz ticks (1 µs per count) and period 19999 gives 20000 counts (20 ms); both registers hold the value minus one. The compare value is then the pulse width in µs, set with `__HAL_TIM_SET_COMPARE`
- The servo only takes a new position every 20 ms, so update the compare value at most once per PWM period; the 1 kHz ADC sampling feeds the filter in between
- At power-up, read and filter the pot and set the compare value before calling `HAL_TIM_PWM_Start`, so the servo goes straight to where the knob points instead of jumping to CubeMX's default 1500 µs first. With DMA sampling, that means waiting for the first full buffer (20 ms)
- Emit the commanded angle over UART as plain text (`POS:90\n`): USART2 to the PC/Pi over the ST-Link USB while developing, USART1 to the Pi's header later. (The Nucleo-64 has no USB connector wired to the F446's own USB peripheral, so USB-CDC isn't an option without extra hardware.) A hobby servo doesn't report its actual position, so this is the angle the Nucleo commanded, not a measurement
- Side exercise: read the same pot through the ADS7830 over I2C and print both readings side by side. The internal reading (0–4095) should be about 16× the external one (0–255) if both use the same reference voltage. Start with an I2C address scan (`HAL_I2C_IsDeviceReady` over 0x08–0x77) and remember that HAL takes the address shifted left by one (`0x4B << 1`)

#### Sampling and filtering the pot

The first working version polls the ADC from the main loop (`HAL_ADC_Start` → `HAL_ADC_PollForConversion` → `HAL_ADC_GetValue`), with the CPU doing every step in sequence. That causes two problems:

- **Sample timing depends on the rest of the loop.** Any delay, print or extra work in the loop shifts when the next sample is taken, so samples arrive at irregular times. A filter needs evenly spaced samples: an average of the last 20 readings only means "the knob's position over the last 20 ms" if they were taken exactly 1 ms apart.
- **The CPU is tied up.** While it waits on a conversion it can't do anything else, and the loop also has to filter, update the servo and send `POS:` lines to the Pi. A loop built out of waits gets harder to manage with every task added.

This step fixes the first with a timer trigger and the second with DMA, then cleans up the readings. Polling would honestly be good enough to make a knob move a servo; the point of doing it this way is a properly working filter, a CPU left free for the UART link, and the standard embedded pattern: a timer paces the hardware, DMA moves the data, and the CPU only processes results.

```
TIM2 (1 kHz) ──TRGO──▶ ADC1 converts ──▶ DMA copies ──▶ adc_buf[20] in RAM
   hardware              hardware          hardware        ▲
                                                           │
                        main loop reads it every 20 ms ────┘
```

**1. Timer-triggered sampling.** A second timer starts each conversion, so samples are exactly 1 ms apart no matter what the CPU is doing.

- **How it works.** Each time a timer's counter wraps back to 0 (its *update event*), it can send a pulse on its **TRGO** (trigger output). TRGO is a connection inside the chip, not a pin. The ADC is told to start a conversion on each TRGO pulse instead of on a software `HAL_ADC_Start`. No code runs when the trigger fires: the pulse goes straight from timer to ADC
- **Why a second timer, not another TIM3 channel.** A timer's prescaler and period are shared by all four of its channels, so every channel runs at the same frequency; only the compare value (pulse width) differs per channel. TIM3 wraps every 20 ms for the servo, so anything it triggers fires at 50 Hz at most, giving one sample per servo period and nothing to average. 1 kHz needs its own counter. Rule of thumb: signals that need different frequencies need different timers; signals that share a frequency and differ only in pulse width can share a timer as separate channels
- **Why 1 kHz.** Not to catch fast motion (a hand turning a knob changes far slower than 10 Hz), but to get 20 samples per 20 ms servo period to average. Anything from a few hundred Hz to a few kHz works; 1 kHz keeps the numbers simple

CubeMX settings:

| Setting | Value | Why |
| --- | --- | --- |
| TIM2 Clock Source | Internal Clock | Turns the timer on, counting from the 84 MHz APB1 timer clock (same as TIM3) |
| TIM2 Prescaler | 83 | 84 MHz ÷ 84 = 1 MHz, one count per µs |
| TIM2 Period | 999 | Counts 0–999 = 1000 µs, so it wraps every 1 ms (1 kHz) |
| TIM2 Trigger Event Selection | Update Event | Sends TRGO at each wrap-around: one pulse per period. No channel or pin is configured |
| ADC1 External Trigger Conversion Source | Timer 2 Trigger Out event | Conversions start on TIM2's pulses, not on software calls |
| ADC1 External Trigger Conversion Edge | Rising | One conversion per pulse |
| ADC1 Continuous Conversion Mode | Disabled | Enabled, the ADC would restart as soon as each conversion finished, as fast as it can, ignoring the timer's pacing |

- The sample time already raised to 84 cycles stays: it gives the pot's 10 kΩ source impedance time to charge the ADC's sampling capacitor
- Check: count conversions in `HAL_ADC_ConvCpltCallback` and print the count once per second. It should read about 1000

**2. DMA into a buffer.** The trigger creates a new problem: the ADC now finishes a conversion every 1 ms and puts it in its data register (`ADC1->DR`), which holds a single value and is overwritten by the next conversion 1 ms later. Each result has to be copied out in time. Three options:

- **Polling:** the CPU checks every millisecond. Back to the CPU being tied up
- **Interrupt per conversion:** works, but it's 1000 interruptions a second to copy 2 bytes each
- **DMA (direct memory access):** a separate hardware unit inside the chip whose only job is moving data. Configured once ("each time the ADC has a result, copy it from `ADC1->DR` into the next slot of this array"), it then runs with no CPU involvement

CubeMX settings:

| Setting | Value | Why |
| --- | --- | --- |
| ADC1 DMA request | Add one | Connects the ADC's "result ready" signal to a DMA stream. The F446 has fixed routing for which stream can serve which peripheral; CubeMX picks a valid one (DMA2, Stream 0 or 4, Channel 0) |
| DMA Mode | Circular | At the end of the array, wrap to slot 0 and keep going, so the buffer always holds the latest samples. *Normal* would stop after one pass |
| Data width | Half Word (both sides) | 16 bits: the 12-bit result fits, 2 bytes per sample, matching `uint16_t` |
| ADC1 DMA Continuous Requests | Enabled | With it disabled, the ADC stops asking DMA for transfers after the first pass, even in circular mode, and sampling silently stops after 20 ms |

- Use a buffer of 20 samples: `volatile uint16_t adc_buf[20]`. At 1 kHz that's exactly one 20 ms servo period, so each servo update averages every sample taken since the last one. `volatile` because DMA changes it behind the compiler's back; without it the compiler may assume the values never change and reuse stale copies
- Start order: `HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, 20)`, then `HAL_TIM_Base_Start(&htim2)`. Arm the receiving end before starting the source: if the timer ran first, conversions would finish with no DMA ready to collect them, values would be lost, and the ADC would flag an *overrun* error. After these two calls the hardware runs on its own; nothing else needs to be called again
- The polled `HAL_ADC_Start` / `HAL_ADC_PollForConversion` / `HAL_ADC_GetValue` block leaves the main loop entirely. The loop now just reads `adc_buf` every 20 ms (`HAL_GetTick`-based, not `HAL_Delay`), filters it and updates the servo
- DMA writes the buffer while the main loop reads it, so a sum can mix samples from two passes. For an average that's harmless. If it ever matters, process each half in `HAL_ADC_ConvHalfCpltCallback` / `HAL_ADC_ConvCpltCallback` while DMA fills the other half

**3. Filter.** Even with the knob still, the raw reading wanders by a few counts from electrical noise. Two standard options, cheapest first:

- **Moving average:** sum the 20-sample buffer and divide by 20. Averaging N samples cuts random noise by about √N (≈4.5× for 20), and it adds about 10 ms of lag (half the window)
- **Exponential (IIR) low-pass:** `filtered += (raw - filtered) / k`, applied once per new value. Larger `k` is smoother but slower. Use integer math: keep `filtered` scaled up (e.g. ×16) so the division doesn't throw away the low bits, and pick `k` as a power of two so the division is a shift
- Start with the moving average alone. Add the IIR on top only if the servo still jitters

**4. Deadband.** Even a filtered value moves by a count or two, and the servo would twitch chasing it. Only send a new pulse width when the filtered reading has moved more than a threshold away from the last value actually sent.

- Size it from measurements, not guesses: hold the knob still, watch the filtered value over UART, and set the threshold just above its peak-to-peak wander
- Useful upper bound: the SG90 datasheet lists a 10 µs dead band, so the servo ignores changes smaller than that anyway. With a 1000 µs pulse span over 4096 counts, 10 µs is about 40 counts. A deadband well under that costs no visible precision
- The cost of a deadband is that small, slow knob movements are ignored until they cross the threshold, then the servo steps. Too large feels sticky; too small lets jitter through

**5. Verify.** Print raw and filtered readings side by side (`RAW:2048 FILT:2046`) at a few Hz:

- Knob still: raw jumps around by several counts, filtered moves by only a count or two
- Knob turned quickly: filtered follows within a few tens of ms. If it visibly lags behind your hand, the filter is too heavy
- Servo: holds still without buzzing or twitching when the knob is untouched

> **Why a pot changes the firmware.** An encoder counts relative steps forever; a pot gives an *absolute* angle over a limited sweep (about 270–300°). That means no counting or overflow handling, but you trade it for analog problems: noise, a non-zero reading floor, and limited resolution (4096 levels across the whole sweep). Filtering, deadband and range-mapping are the standard answers.

> **Why a servo changes the firmware.** A hobby servo has its own position feedback and control loop inside it: the firmware commands an angle and the servo gets itself there. There's no step counting and no homing problem, unlike an open-loop stepper. The trade-offs: travel is limited to about 180°, so the pot's 270–300° sweep is scaled down rather than followed 1:1; speed and smoothness are up to the servo; and there's no way to read back where it actually is.

### Pi 5 side

**1. Minimal custom image with Yocto**

- Start from `poky` + `meta-raspberrypi`
- Trim it to a minimal `core-image-minimal`-derived image: no desktop, only what's needed (a shell, the app, systemd)
- A custom layer (`meta-knob-demo`) contains the app recipe and driver recipe, demonstrating the layer/recipe model rather than just running `bitbake`
- Buildroot is a legitimate substitute for faster iteration and a shallower learning curve; Yocto is what most companies with real product lines use

**2. Device tree overlay**

- The Nucleo's USART1 (PA9 TX on D8, PA10 RX on D2) is wired to the Pi's header UART (GPIO14 TX, GPIO15 RX; TX↔RX, RX↔TX, shared GND through the breadboard's ground rail; both are 3.3 V logic). An overlay adds a **child node under that UART** describing the Nucleo, with its own `compatible` string (e.g. `"<yourname>,tether"`). This is the standard device tree pattern for a device attached to a serial port, and it's what lets the kernel bind the driver automatically
- The Pi 5's dedicated 3-pin debug UART connector carries the serial console, so the header UART stays free for the Nucleo

**3. Kernel driver**

- A **serdev** driver (the kernel's framework for devices attached to a UART, used by in-tree drivers for Bluetooth and GNSS chips wired to serial ports) that:
  - Binds to the device tree node by its `compatible` string, opens the UART and sets the baud rate, and receives incoming bytes through serdev's receive callback, parsing `POS:` lines
  - Exposes current position via `sysfs` (e.g. `/sys/class/knob_demo/position`) or a `read()`/`ioctl()` interface
  - Optionally accepts a target position written to a `sysfs` attribute, forwarded to the Nucleo as a setpoint command
- Aim for a correct, clean, well-commented driver of ~200–400 lines rather than something elaborate and unstable

**4. User-space app**

- A small daemon (C or Python) that reads `sysfs`/the device node and does something with it: log to a file, expose over a local socket, drive an LED
- Packaged as a systemd service via the Yocto recipe, not run by hand

**5. CI (optional)**

- GitHub Actions job that runs `bitbake` for the image (or at least lints the layer/recipes) and checks the driver builds against a pinned kernel headers version

## Repository layout

The STM32 firmware is a CubeMX-generated CMake project at the repository root. CubeMX regenerates `Core/`, `Drivers/` and the build files in place, and only keeps hand-written code inside `USER CODE BEGIN/END` blocks. The Pi-side pieces go in their own directories next to it:

```
.
├── Tether.ioc         # CubeMX configuration (pins, clocks, peripherals)
├── Core/              # Firmware sources: main.c, HAL MSP init, interrupts
├── Drivers/           # STM32 HAL + CMSIS (generated, don't edit)
├── cmake/, CMakeLists.txt, CMakePresets.json, startup + linker script
├── driver/            # serdev kernel driver (out-of-tree module)
├── dts/               # Device tree overlay for the header-UART child node
├── app/               # User-space daemon + systemd unit
├── meta-knob-demo/    # Yocto layer with recipes for the driver, app and image
├── docs/              # Plan, datasheets, design notes, debugging writeup, wiring photos
└── README.md          # (to add)
```

## Build order

- [ ] Nucleo, one component at a time:
  - [ ] UART printing to the PC over the ST-Link virtual COM port (the debug channel for everything after it)
  - [ ] Pot alone: check the wiper sweeps 0–3.3 V with a multimeter, then a polled ADC read on PA0, printing raw counts
  - [ ] Timer-triggered ADC with DMA at 1 kHz, plus filter and deadband (see [Sampling and filtering the pot](#sampling-and-filtering-the-pot)):
    - [ ] TIM2 TRGO at 1 kHz triggering ADC1; check the rate by counting conversions per second
    - [ ] Circular DMA into a 20-sample buffer; main loop no longer calls `HAL_ADC_PollForConversion`
    - [ ] Moving average over the buffer; print raw and filtered side by side
    - [ ] Measure the filtered value's wander with the knob still, and set the deadband just above it
    - [ ] (If still jittery) add an IIR low-pass on top
  - [ ] Servo alone, pot ignored: PWM on PA6 (D12), check the 50 Hz period and pulse width with a logic analyzer (or an LED) before connecting the servo, then sweep through fixed angles to find its real pulse-width limits
  - [ ] Close the loop: map the filtered pot reading onto the servo's pulse width
  - [ ] Emit `POS:` lines over UART
- [ ] (Side exercise) ADS7830 over I2C: address scan, read one channel, compare against the internal ADC on the same pot
- [ ] Pi: get a stock image talking to the Nucleo over USB-serial first, as cheap end-to-end validation before investing in Yocto/driver work
- [ ] Stand up a minimal Yocto build (`poky` + `meta-raspberrypi`) that boots on the Pi 5 and reaches a login prompt over the serial console
- [ ] Move the Nucleo link from USB to the Pi's header UART (enable USART1 on the Nucleo), and write the device tree overlay that adds a child node for it under that UART
- [ ] Write the serdev driver, bind it via the overlay's `compatible` string, expose position through `sysfs`
- [ ] Write the user-space daemon, package it as a systemd service
- [ ] Fold the daemon + driver into the `meta-knob-demo` layer so the whole thing builds as one image
- [ ] Write the debugging writeup and design notes
- [ ] (Optional) Add CI

## How it fits together

1. **Get the servo and potentiometer working on the Nucleo alone.** On the breadboard, wire the potentiometer to 3.3 V, GND and one ADC pin, and the servo's signal wire to one timer pin, with the servo powered from its own 5 V supply. Write firmware that samples the knob's position with the ADC, smooths it, and sets the servo's pulse width to match. No Linux involved yet; just prove the physical loop works, watched over a basic serial print.
2. **Get the Pi talking to the Nucleo the easy way first.** Plug the Nucleo into the Pi over USB. The Nucleo shows up as a serial device. A five-line Python script on stock Raspberry Pi OS reads the position numbers coming across. This proves the wiring and the link work before adding complexity.
3. **Build a minimal Linux image for the Pi instead of using the stock one.** Use Yocto (or Buildroot, which is easier to start with) to build a stripped-down OS image: just enough to boot, log in over a serial cable, and run the app.
4. **Move the connection from USB to the UART pin header, and describe it in the device tree.** Instead of the Nucleo showing up automatically as a generic USB device, wire it to actual UART pins and write a small device tree overlay telling Linux "there's a device here, on this port, talk to it this way."
5. **Write a small kernel driver for it.** A short piece of kernel code, using the kernel's **serdev** framework for UART-attached devices, that the kernel attaches automatically when it finds the device tree entry. It owns the connection to the Nucleo and exposes the servo's position as a simple file Linux apps can read (under `/sys/...`).
6. **Write a small app that uses the driver.** A simple program that reads that file and does something with it: logs it, prints it. It talks to the driver, not directly to the serial port.
7. **Wire it all together into the Yocto build.** Add the driver and app as recipes in the `meta-knob-demo` layer, so the whole system (OS, driver, app) builds into one image from scratch.
8. **Write down what broke and how it was fixed.** Keep a running note of at least one real bug (image didn't boot, driver crashed, device tree didn't bind) and how it was diagnosed.

## Documentation

- **Debugging writeup** (`docs/debugging.md`): at least one real story, such as something that didn't boot, a driver that oopsed, or a device tree binding that didn't match, and how it was found and fixed using `dmesg`, the serial console, or a logic analyzer.
- **Design notes** (`docs/design.md`): the kernel/user-space boundary. Why the driver does X and the app does Y, and what was considered and rejected (e.g. "considered doing everything in user-space via raw serial, chose a driver + sysfs interface because ..."). Also worth recording: why a servo rather than a stepper, and why the main loop uses the internal ADC rather than the ADS7830.
