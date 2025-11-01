# Hardware installation

We refer to this wiring diagram for wiring the SmartEVSE.
![wiring_diagram_3phasev3 1](https://github.com/user-attachments/assets/51962f3d-0c65-4e56-824c-7f2554e21d88)

In above wiring diagram, the Sensorbox is measuring the mains currents using CT clamps on the Mains phase wires.<br>
It's also possible to connect it directly to the P1 port of the smartmeter, as long as the smartmeter is using the DSMR 5.0 standard.<br>

> [!NOTE]
> Please use normal contactors like the IKA432-40 from Iskra, as energy efficient (AC/DC) or silent contactors (ABB ESBxx series, Hager ESCxxx<b>S</b>) will not work correctly!<br>

> [!TIP]
> Don't forget to connect the PE/ground wire to the SmartEVSE!

> [!WARNING]
> The EVSE needs to be protected with a circuit breaker and residual- current circuit breaker.

# Supported modbus kWh meters
The following meters are directly supported as Mains or EV meter:
- PHOENIX CONTACT EEM-350-D-MCB
- Finder 7E.78.8.400.0212
- Finder 7M.38.8.400.0212
- Eastron SDM630, SDM230, SDM72D
- ABB B23 212-100
- Sinotimer DTS6619
- WAGO 879-30x0
- Inepro PRO-380-Mod (should be configured as WAGO 879-30x0, seems to be identical)
- Schneider iEM3x5x
- Chint DTSU666
- Carlo Gavazzi EM340

If your meter is not listed, you might be able to use the Custom option in the menu, that let's you enter each register manually.<br>
Connect the A and B of your meter to the A and B terminals of the SmartEVSE.<br>
Note that the ABB and Carlo Gavazzi meter have the A/B signals reversed, you should connect A to B, and B to A on these meters.<br>
You can use Cat5 network cable for the wiring between SmartEVSE(s), kWh meter(s) and Sensorbox.<br>
Make sure to use one twisted pair for A and B, so for example A=Green, B=Green/White<br>
<br>
Baudrate: 9600 bps<br>
Parity: NONE<br>
Stopbits: 1<br>
As address 1-10 are reserved for SmartEVSE communication and the Sensorbox, make sure to set the modbus address of the Mains or EV meter to address 11 or higher.<br>

# Inverted wiring of kWh meter
If you are using a 3 phase Eastron kWh meter, you can feed it from below (like in most Dutch power panels). Now the polarity of currents is reversed, so in the MainsMeter or EVMeter configuration you should choose kWh meter type "Inverted Eastron".

# Subpanel or "garage" configuration
If you have other current-users on a Subpanel, use this wiring and the added configuration:

                             mains
                               |
                        [main breaker 25A]
                               |
                        [kWh meter "Mains"]
                               |
            -----------------------------------
            |            |                    |
                [group breaker 16A]   [subpanel breaker 16A]
                                              |
                                       [kWh meter "EV"]
                                              |
                                        ----------------
                                        |              |
                            [washer breaker 16A]  [smartevse breaker 16A]

   In this example you configure Mains to 25A, MaxCircuit to 16A; the charger will limit itself so that neither the 25A mains nor the 16A from the subpanel will be
   exceeded...
   Note that for this functionality you will need to be in Smart or Solar mode; it is no longer necessary to enable Load Balancing for this function.

# Second Contactor C2
One can add a second contactor (C2) that switches off 2 of the 3 phases of a three-phase Mains installation; this can be useful if one wants to charge of off
Solar; EV's have a minimal charge current of 6A, so switching off 2 phases allows you to charge with a current of 6-18A, while 3 phases have a minimum current
of 3x6A=18A. This way you can still charge solar-only on smaller solar installations.

One should wire C2 according to this schema:

<img src="https://github.com/user-attachments/assets/2d9711cc-7248-4fdd-9e61-8089302ab63b" alt="SmartESVE Contact2 diagram" width="500">

This way the (dangerous) situation is avoided that some Phases are switched ON, and Neutral is switched OFF.<br>
Note that it is important that you actually DO NOT switch the L1 pin of the CCS plug with the C2 contactor; some cars (e.g. Tesla Model 3) will go into error;
they expect the charging phase to be on the L1 pin when single-phase charging.<br>
Note also that in case the phases cannot be detected automatically (especially when no EVmeter is connected), and SmartEVSE _knows_ it is charging at a single
phase (e.g. because Contact2 is at "Always Off"), it assumes that L1 is the phase we are charging on!<br>

# Multiple SmartEVSE controllers on one mains supply (Power Share)
Up to eight SmartEVSE modules can share one mains supply.

Hardware connections:
* Connect the A, B and GND connections from the Master to the Node(s).
* So A connects to A, B goes to B etc.
* If you are using the Sensorbox, you should connect the A, B, +12V and GND wires from the sensorbox to the same screw terminals of the SmartEVSE!
Make sure that the +12V wire from the sensorbox is connected to only -one– SmartEVSE.


