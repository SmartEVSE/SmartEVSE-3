# Hardware installation

We refer to [this wiring diagram](SmartEVSEv3_build.pdf) for wiring the SmartEVSE.


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
    - one should wire C2 according to this schema:

            N    L1   L2   L3
            |    |    |    |
          --------------------
          | 4-p contactor C1 |
          --------------------
            |    |    |    |
            |    | ------------------
            |    | |2-p contactor C2|
            |    | ------------------
            |    |    |    |
          --------------------
          |    EV-cable      |
          --------------------

      This way the (dangerous) situation is avoided that some Phases are switched ON, and Neutral is switched OFF.
      Note that it is important that you actually DO NOT switch the L1 pin of the CCS plug with the C2 contactor; some cars (e.g. Tesla Model 3) will go into error;
      they expect the charging phase to be on the L1 pin when single-phase charging...
      Note also that in case the phases cannot be detected automatically (especially when no EVmeter is connected), and SmartEVSE _knows_ it is charging at a single
      phase (e.g. because Contact2 is at "Always Off"), it assumes that L1 is the phase we are charging on!!

      By default C2 is switched OFF ("Not present"); if you want to keep on charging on 3 phases after installing C2, you should change the setting Contact2 in the
      Setup Menu.

# Multiple SmartEVSE controllers on one mains supply (Power Share)
Up to eight SmartEVSE modules can share one mains supply.
Hardware connections
* Connect the A, B and GND connections from the Master to the Node(s).
* So A connects to A, B goes to B etc.
* If you are using Smart/Solar mode, you should connect the A, B , +12V and GND wires from the sensorbox to the same screw terminals of the SmartEVSE! Make sure that the +12V  wire from the sensorbox is connected to only  -one– SmartEVSE.

