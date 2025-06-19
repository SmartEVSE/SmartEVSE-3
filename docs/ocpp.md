# User experiences with OCPP backend providers

## Tap Electric

You can connect your charging station to the Tap Electric platform at no cost. This allows you to make your charging station available to other users through the app. Users without a subscription pay a 10% transaction fee per charging session to Tap Electric (this is their business case to keep the connection free of charge). This fee is automatically added to the cost of the charging session the start at your station. The charging station owner receives his earnings monthly, minus any applicable service fees, as agreed with Tap Electric. There is no upfront amount held by Tap Electric.

- Sign up and download the app
- Go to "Beheer"
- Connect a new charger and select the option "my charger is not in the list"
- Copy the link wss://ocpp.tapelectric.app/XXXXXXXX and paste it into your OCPP settings of the SmartEVSE (Backend URL)
- For "Charge Box Id," enter an id of your choice
- You can leave the password blank
- Save the settings and you should see your charging station appear in TapElectric


In the TapElectric app, you can adjust various settings, including the rate you charge, etc.

You decide the price you want to charge for charging sessions. You can set rates based on your electricity contract (for example, day and night tariffs) and include hardware costs in your pricing. Tap Electric ensures that charging and payment are fully automated via the app. You link your bank account to the Tap Electric platform.
Tap Electric can pay the VAT to the tax authorities on your behalf for the charging sessions, depending on how your account is set up. This is especially relevant if you don’t have a VAT number but still want to pay VAT on the transactions. In that case, Tap Electric collects VAT over the full transaction amount and remits it to the authorities. Every month you receive a clear invoice from Tap Electric, showing what has been paid and to whom. You do not need to file a separate VAT return for these charging sessions yourself.
You can determine who has access to your charging station (for example, residents, guests, or employees) and whether you want to offer free or discounted charging to specific user groups. You have real-time insight into the usage of your charging station and can manage its status and availability via the app.

Everything is working in combination with a business Shell Recharge charge card, by using the RFID reader.

If you don't want to use an RFID card (e.g. your charger is mounted inside your garage and you only have one EV):
- on the web-dashboard of your SmartEVSE, check the checkbox Auto Authorize
- add the ID of your charge card. You can get the ID by scanning the card with your phone using an app like "NFC tools"


Only issue I encounter is that sometimes the connection between the SmartEVSE and Tap is terminated. I have to reboot the SmartEVSE and check in the TAP app that the connection is restored. A disconnected SmartEVSE can still be used to charge. It will upload the latests reading when the connection is restored. If there are gaps in charge sessions, you can contact the TAP support desk to solve it.



## Tibber

I'm using it in combination with Tibber and Myenergi Zappi (v2.1).

I googled on pictures regarding a correct serial number from a zappi and i modified the last digit, and it was possible to be used in the Tibber App.

Open Tibber app and follow these steps:
- Open the "Power-up" menu.
- Press "Laden".
- Select "Myenergi Zappi (v2.1).
- Enter a serial number from a zappi (adjusted serial numer)

Enter the information provided on screen in the evse OCPP configuration:
- BackendURL: wss://zappi.ocpp-s.tibber.com/
- Charge Box ID: Already filled in from "serial number" step.
- Password: Provided from Tibber
- I also enabled auto-authorize but used a code of my own (I don't have an rfid reader connected to SmartEvse).

To enable gridrewards you also need to add a car:
- Open the "Power-up" menu.
- Press "Laden".
- Select car you want, i selected a dummy as i don't want to connect my car to the Tibber app.

As final step you need to select the chargepoint in the "grid rewards" part of the app. Otherwise it will not enable it for you and allow you to charge using grid rewards. The detection of the car being connected is used from the SmartEvse, when you connect the car.

