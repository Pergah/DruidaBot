void handleNewMessages(int numNewMessages) {
  Serial.print("handleNewMessages ");
  Serial.println(numNewMessages);

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    int valor = text.toInt();

    String from_name = bot.messages[i].from_name;
    if (from_name == "")
      from_name = "Guest";

  //  int commandCode = getCommandCode(text);


    //MENU PRINCIPAL

    if (text == "/start") {
      String welcome = "Bienvenido al Druida Bot " + from_name + ".\n";
      welcome += "Ingrese un comando: \n";
      welcome += "/config \n";
      welcome += "/manual \n";
      welcome += "/auto \n";
      welcome += "/status \n";
      welcome += "/infoconfig \n";
      welcome += "/DiasRiego \n";
      welcome += "/resetDruidaBot \n";
      welcome += "/enviarData";
      bot.sendMessage(chat_id, welcome, "Markdown");
      delay(500);
    }

    //MODO MANUAL


    if (text == "/manual" || modoMenu == MANUAL) {
      modoMenu = MANUAL;
      String modoManu = "MODO MANUAL \n";
      modoManu += "/R1on - /R1off - /R1onTime\n";
      modoManu += "/R2on - /R2off - /R2onTime\n";
      modoManu += "/R2iron - /R2iroff - /R2ironTime\n";
      modoManu += "/R3on - /R3off - /R3onTime\n";
      modoManu += "/R4on - /R4off - /R4onTime\n";
      modoManu += "/controlRemoto \n";
      modoManu += "/manual \n";
      bot.sendMessage(chat_id, modoManu, "Markdown");
      delay(500);



      if (text == "/R1on") {

        modoR1 = MANUAL;
        estadoR1 = 1;


        bot.sendMessage(chat_id, "Rele 1 is ON", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/R1off") {
        modoR1 = MANUAL;
        estadoR1 = 0;
        bot.sendMessage(chat_id, "Rele 1 is OFF", "");
        Guardado_General();
        delay(500);
      }


      if (text == "/R2on") {

        modoR2 = MANUAL;
        estadoR2 = 1;
        bot.sendMessage(chat_id, "Rele 2 is ON", "");
        Guardado_General();
        delay(500);
      }

        if (text == "/R2iron") {

        modoR2ir = MANUAL;
        estadoR2ir = 1;
        bot.sendMessage(chat_id, "Rele 2 (IR) is ON", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/R2off") {
        modoR2 = MANUAL;
        estadoR2 = 0;
        bot.sendMessage(chat_id, "Rele 2 is OFF", "");
        Guardado_General();
        delay(500);
      }

            if (text == "/R2iroff") {
        modoR2ir = MANUAL;
        estadoR2ir = 0;
        bot.sendMessage(chat_id, "Rele 2 (IR) is OFF", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/R3on") {

        modoR3 = MANUAL;
        estadoR3 = 1;
        bot.sendMessage(chat_id, "Rele 3 is ON", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/R3off") {
        modoR3 = MANUAL;
        estadoR3 = 0;
        bot.sendMessage(chat_id, "Rele 3 is OFF", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/R4on") {

        modoR4 = MANUAL;
        estadoR4 = 1;
        bot.sendMessage(chat_id, "Rele 4 is ON", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/R4off") {
        modoR4 = MANUAL;
        estadoR4 = 0;
        bot.sendMessage(chat_id, "Rele 4 is OFF", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/controlRemoto") {
        Serial.println("enviando señal IR...");
        irsend.sendRaw(IRsignal, 72, 38);  // Envía la señal IR ajustada con frecuencia de 38 kHz
        delay(1000);                       // Espera 10 segundos antes de volver a emitir la señal
        bot.sendMessage(chat_id, "Señal IR enviada", "");
        delay(500);
      }

      // Lógica para encender los relés por un tiempo determinado
      if (text == "/R1onTime") {
        esperandoTiempoR1 = true; // Establece que estamos esperando el tiempo de encendido para R1
        bot.sendMessage(chat_id, "¿Por cuánto tiempo (en segundos) quieres encender el Rele 1?", "");
        delay(500);
      }

      if (text == "/R2onTime") {
        esperandoTiempoR2 = true; // Establece que estamos esperando el tiempo de encendido para R2
        bot.sendMessage(chat_id, "¿Por cuánto tiempo (en segundos) quieres encender el Rele 2?", "");
        delay(500);
      }

      if (text == "/R2ironTime") {
        esperandoTiempoR2ir = true; // Establece que estamos esperando el tiempo de encendido para R2
        bot.sendMessage(chat_id, "¿Por cuánto tiempo (en segundos) quieres encender el Rele 2 (IR)?", "");
        delay(500);
      }

      if (text == "/R3onTime") {
        esperandoTiempoR3 = true; // Establece que estamos esperando el tiempo de encendido para R3
        bot.sendMessage(chat_id, "¿Por cuánto tiempo (en segundos) quieres encender el Rele 3?", "");
        delay(500);
      }

      if (text == "/R4onTime") {
        esperandoTiempoR4 = true; // Establece que estamos esperando el tiempo de encendido para R4
        bot.sendMessage(chat_id, "¿Por cuánto tiempo (en segundos) quieres encender el Rele 4?", "");
        delay(500);
      }

      // Espera un nuevo mensaje del usuario para ingresar el tiempo
      if (esperandoTiempoR1 && text != "/R1onTime") {
        tiempoR1 = text.toInt(); // Convierte el texto ingresado en un número entero
        if (tiempoR1 > 0) { // Si el valor ingresado es válido
          esperandoTiempoR1 = false; // Resetea la variable
          encenderRele1PorTiempo(tiempoR1); // Enciende el relé 1 por el tiempo indicado
        } else {
          bot.sendMessage(chat_id, "Por favor ingresa un valor válido en segundos.", "");
        }
        delay(500);
      }

      if (esperandoTiempoR2 && text != "/R2onTime") {
        tiempoR2 = text.toInt(); // Convierte el texto ingresado en un número entero
        if (tiempoR2 > 0) { // Si el valor ingresado es válido
          esperandoTiempoR2 = false; // Resetea la variable
          encenderRele2PorTiempo(tiempoR2); // Enciende el relé 2 por el tiempo indicado
        } else {
          bot.sendMessage(chat_id, "Por favor ingresa un valor válido en segundos.", "");
        }
        delay(500);
      }

            if (esperandoTiempoR2ir && text != "/R2ironTime") {
        tiempoR2ir = text.toInt(); // Convierte el texto ingresado en un número entero
        if (tiempoR2ir > 0) { // Si el valor ingresado es válido
          esperandoTiempoR2ir = false; // Resetea la variable
          encenderRele2irPorTiempo(tiempoR2ir); // Enciende el relé 2 por el tiempo indicado
        } else {
          bot.sendMessage(chat_id, "Por favor ingresa un valor válido en segundos.", "");
        }
        delay(500);
      }

      if (esperandoTiempoR3 && text != "/R3onTime") {
        tiempoR3 = text.toInt(); // Convierte el texto ingresado en un número entero
        if (tiempoR3 > 0) { // Si el valor ingresado es válido
          esperandoTiempoR3 = false; // Resetea la variable
          encenderRele3PorTiempo(tiempoR3); // Enciende el relé 3 por el tiempo indicado
        } else {
          bot.sendMessage(chat_id, "Por favor ingresa un valor válido en segundos.", "");
        }
        delay(500);
      }

      if (esperandoTiempoR4 && text != "/R4onTime") {
        tiempoR4 = text.toInt(); // Convierte el texto ingresado en un número entero
        if (tiempoR4 > 0) { // Si el valor ingresado es válido
          esperandoTiempoR4 = false; // Resetea la variable
          encenderRele4PorTiempo(tiempoR4); // Enciende el relé 4 por el tiempo indicado
        } else {
          bot.sendMessage(chat_id, "Por favor ingresa un valor válido en segundos.", "");
        }
        delay(500);
      }
    }


    // MODO AUTOMATICO

    if (text == "/auto" || modoMenu == AUTO) {
      modoMenu = AUTO;
      String modoManu = "MODO AUTOMATICO: \n";
      modoManu += "/R1auto\n";
      modoManu += "/R1timer\n";
      modoManu += "/R2auto\n";
      modoManu += "/R2irauto\n";
      modoManu += "/R3auto\n";
      modoManu += "/R3autoParam\n";
      modoManu += "/R4auto\n";
      bot.sendMessage(chat_id, modoManu, "Markdown");
      delay(500);


      if (text == "/R1auto") {

        modoR1 = AUTO;
        bot.sendMessage(chat_id, "Rele 1 Automatico", "");
        Guardado_General();
        delay(500);
      }

        if (text == "/R1timer") {

        modoR1 = TIMER;
        bot.sendMessage(chat_id, "Rele 1 Automatico (TIMER)", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/R2auto") {
        modoR2 = AUTO;
        bot.sendMessage(chat_id, "Rele 2 Automatico", "");
        Guardado_General();
        delay(500);
      }

            if (text == "/R2irauto") {
        modoR2ir = AUTO;
        bot.sendMessage(chat_id, "Rele 2 (IR) Automatico", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/R3auto")

      {

        modoR3 = AUTO;
        bot.sendMessage(chat_id, "Rele 3 Automatico", "");
        Guardado_General();
        delay(500);
      }

            if (text == "/R3autoParam")

      {

        modoR3 = AUTOINT;
        bot.sendMessage(chat_id, "Rele 3 Automatico inteligente", "");
        Guardado_General();
        delay(500);
      }

      if (text == "/R4auto") {
        modoR4 = AUTO;
        bot.sendMessage(chat_id, "Rele 4 Automatico", "");
        Guardado_General();

        delay(500);
      }
    }


    //MODO CONFIG

    String modoConf = "MODO CONFIG: \n";
    modoConf += "/infoconfig\n";
    modoConf += "/minR1config\n";
    modoConf += "/maxR1config\n";
    modoConf += "/paramR1config\n";
    modoConf += "/horaOnR1config\n";
    modoConf += "/minOnR1config\n";
    modoConf += "/horaOffR1config\n";
    modoConf += "/minOffR1config\n";
    modoConf += "/minR2config\n";
    modoConf += "/maxR2config\n";
    modoConf += "/paramR2config\n";
    modoConf += "/minR2irconfig\n";
    modoConf += "/maxR2irconfig\n";
    modoConf += "/paramR2irconfig\n";
    modoConf += "/horaOnR3config\n";
    modoConf += "/minOnR3config\n";
    modoConf += "/horaOffR3config\n";
    modoConf += "/minOffR3config\n";
    modoConf += "/minR3config\n";
    modoConf += "/maxR3config\n";
    modoConf += "/horaOnR4config\n";
    modoConf += "/minOnR4config\n";
    modoConf += "/horaOffR4config\n";
    modoConf += "/minOffR4config\n";


    if (text == "/config") {
      bot.sendMessage(chat_id, modoConf, "Markdown");

      modoMenu = CONFIG;
    }

    /// R1

    if (text == "/minR1config") {
      modoR1 = CONFIG;
      modoMenu = CONFIG;
      R1config = 1;
      bot.sendMessage(chat_id, "Ingrese valor Min R1: ");
    }
    if (R1config == 1) {
      minR1 = text.toFloat();  //Ingresa valor por mensaje de telegram

      if (minR1 > 0 && minR1 < 100) {
        Serial.print("Valor min R1: ");
        Serial.println(minR1);
        bot.sendMessage(chat_id, "Valor min R1 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R1config = 0;
      }
    }

    ///

    if (text == "/maxR1config") {
      modoR1 = CONFIG;
      modoMenu = CONFIG;
      R1config = 2;
      bot.sendMessage(chat_id, "Ingrese valor Max R1: ");
    }
    if (R1config == 2) {
      maxR1 = text.toFloat();

      if (maxR1 > 0) {
        Serial.print("Valor max R1: ");
        Serial.println(maxR1);
        bot.sendMessage(chat_id, "Valor max R1 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R1config = 0;
      }
    }

    ///

    if (text == "/paramR1config") {
      modoR1 = CONFIG;
      modoMenu = CONFIG;
      R1config = 3;
      bot.sendMessage(chat_id, "Ingrese parametro R1: \n1- Humedad.\n2- Temperatura.\n3- DPV.\n4- Temp Agua.");
    }
    if (R1config == 3) {
      paramR1 = text.toInt();

      if (paramR1 > 0) {
        Serial.print("Param R1: ");
        Serial.println(paramR1);
        bot.sendMessage(chat_id, "Valor param R1 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R1config = 0;
      }
    }

        if (text == "/horaOnR1config") {
      modoR1 = CONFIG;
      modoMenu = CONFIG;
      R1config = 4;
      bot.sendMessage(chat_id, "Ingrese parametro Hora On R1: ");
    }
    if (R1config == 4) {
      horaOnR1 = text.toInt();

      if (horaOnR1 > 0) {
        Serial.print("Hora On R1: ");
        Serial.println(horaOnR1);
        bot.sendMessage(chat_id, "Valor horaOnR1 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R1config = 0;
      }
    }

      if (text == "/horaOffR1config") {
      modoR1 = CONFIG;
      modoMenu = CONFIG;
      R1config = 5;
      bot.sendMessage(chat_id, "Ingrese Hora Off R1:");
    }
    if (R1config == 5) {
      horaOffR1 = text.toInt();

      if (horaOffR1 > 0) {
        Serial.print("Hora Off R1: ");
        Serial.println(horaOffR1);
        bot.sendMessage(chat_id, "Valor horaOffR1 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R1config = 0;
      }
    }

          if (text == "/minOnR1config") {
      modoR1 = CONFIG;
      modoMenu = CONFIG;
      R1config = 6;
      bot.sendMessage(chat_id, "Ingrese Min On R1:");
    }
    if (R1config == 6) {
      minOnR1 = text.toInt();

      if (minOnR1 > 0) {
        Serial.print("Min On R1: ");
        Serial.println(minOnR1);
        bot.sendMessage(chat_id, "Valor minOnR1 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R1config = 0;
      }
    }

              if (text == "/minOffR1config") {
      modoR1 = CONFIG;
      modoMenu = CONFIG;
      R1config = 7;
      bot.sendMessage(chat_id, "Ingrese Min Off R1:");
    }
    if (R1config == 7) {
      minOffR1 = text.toInt();

      if (minOffR1 > 0) {
        Serial.print("Min Off R1: ");
        Serial.println(minOffR1);
        bot.sendMessage(chat_id, "Valor minOffR1 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R1config = 0;
      }
    }

    /// R2

    if (text == "/minR2config") {
      modoR2 = CONFIG;
      modoMenu = CONFIG;
      R2config = 1;
      bot.sendMessage(chat_id, "Ingrese valor Min R2: ");
    }
    if (R2config == 1) {
      minR2 = text.toFloat();

      if (minR2 > 0) {
        Serial.print("Valor min R2: ");
        Serial.println(minR2);
        bot.sendMessage(chat_id, "Valor min R2 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R2config = 0;
      }
    }


    if (text == "/maxR2config") {
      modoR2 = CONFIG;
      modoMenu = CONFIG;
      R2config = 2;
      bot.sendMessage(chat_id, "Ingrese valor Max R2: ");
    }
    if (R2config == 2) {
      maxR2 = text.toFloat();

      if (maxR2 > 0) {
        Serial.print("Valor max R2: ");
        Serial.println(maxR2);
        bot.sendMessage(chat_id, "Valor max R2 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R2config = 0;
      }
    }

        if (text == "/paramR2config") {
      modoR2 = CONFIG;
      modoMenu = CONFIG;
      R2config = 3;
      bot.sendMessage(chat_id, "Ingrese parametro R2: \n1- Humedad.\n2- Temperatura.\n3- DPV.\n4- Temp Agua.");
    }
    if (R2config == 3) {
      paramR2 = text.toInt();

      if (paramR2 > 0) {
        Serial.print("Param R2: ");
        Serial.println(paramR2);
        bot.sendMessage(chat_id, "Valor param R2 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R2config = 0;
      }
    }


    // R2 IR

    if (text == "/minR2irconfig") {
      modoR2ir = CONFIG;
      modoMenu = CONFIG;
      R2irconfig = 1;
      bot.sendMessage(chat_id, "Ingrese valor Min R2 (IR): ");
    }
    if (R2irconfig == 1) {
      minR2ir = text.toFloat();

      if (minR2ir > 0) {
        Serial.print("Valor min R2 (IR): ");
        Serial.println(minR2ir);
        bot.sendMessage(chat_id, "Valor min R2 (IR) guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R2irconfig = 0;
      }
    }

    if (text == "/maxR2irconfig") {
      modoR2ir = CONFIG;
      modoMenu = CONFIG;
      R2irconfig = 2;
      bot.sendMessage(chat_id, "Ingrese valor Max R2 (IR): ");
    }
    if (R2irconfig == 2) {
      maxR2ir = text.toFloat();

      if (maxR2ir > 0) {
        Serial.print("Valor max R2 (IR): ");
        Serial.println(maxR2ir);
        bot.sendMessage(chat_id, "Valor max R2 (IR) guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R2irconfig = 0;
      }
    }

    ///

    if (text == "/paramR2irconfig") {
      modoR2ir = CONFIG;
      modoMenu = CONFIG;
      R2irconfig = 3;
      bot.sendMessage(chat_id, "Ingrese parametro IR: \n1- Humedad.\n2- Temperatura.\n3- DPV.\n4- Temp Agua.");
    }
    if (R2irconfig == 3) {
      paramR2ir = text.toInt();

      if (paramR2ir > 0) {
        Serial.print("Param R2 (IR): ");
        Serial.println(paramR2ir);
        bot.sendMessage(chat_id, "Valor param R2 (IR) guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R2irconfig = 0;
      }
    }

    /// RELE 3 CONFIG

    if (text == "/horaOnR3config") {
      modoR3 = CONFIG;
      modoMenu = CONFIG;
      R3config = 1;
      bot.sendMessage(chat_id, "Ingrese hora de Encendido R3: ");
    }
    if (R3config == 1) {
      horaOnR3 = text.toInt();
      if (horaOnR3 > 0) {
        Serial.print("Hora encendido R3: ");
        Serial.println(horaOnR3);
        bot.sendMessage(chat_id, "Valor hora Encendido Rele 3 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R3config = 0;
      }
    }

    if (text == "/minOnR3config") {
      modoR3 = CONFIG;
      modoMenu = CONFIG;
      R3config = 2;
      bot.sendMessage(chat_id, "Ingrese minuto de encendido R3: ");
    }
    if (R3config == 2) {
      minOnR3 = text.toInt();
      if (minOnR3 > 0) {
        Serial.print("Minuto de encendido: ");
        Serial.println(minOnR3);
        bot.sendMessage(chat_id, "Valor minuto de encendido R3 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R3config = 0;
      }
    }

    if (text == "/horaOffR3config") {
      modoR3 = CONFIG;
      modoMenu = CONFIG;
      R3config = 3;
      bot.sendMessage(chat_id, "Ingrese hora de apagado R3: ");
    }
    if (R3config == 3) {
      horaOffR3 = text.toInt();
      if (horaOffR3 > 0) {
        Serial.print("Hora de apagado: ");
        Serial.println(horaOffR3);
        bot.sendMessage(chat_id, "Hora de apagado R3 modificado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R3config = 0;
      }
    }

    if (text == "/minOffR3config") {
      modoR3 = CONFIG;
      modoMenu = CONFIG;
      R3config = 4;
      bot.sendMessage(chat_id, "Ingrese minuto de apagado R3: ");
    }
    if (R3config == 4) {
      minOffR3 = text.toInt();
      if (minOffR3 > 0) {
        Serial.print("Minuto de apagado: ");
        Serial.println(minOffR3);
        bot.sendMessage(chat_id, "Valor minuto de apagado R3 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R3config = 0;
      }
    }

    if (text == "/minR3config") {
      modoR3 = CONFIG;
      modoMenu = CONFIG;
      R3config = 5;
      bot.sendMessage(chat_id, "Ingrese valor min R3: ");
    }
    if (R3config == 5) {
      minR3 = text.toFloat();

      if (minR3 > 0) {
        Serial.print("Valor min R3: ");
        Serial.println(minR3);
        bot.sendMessage(chat_id, "Valor min R3 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R3config = 0;
      }
    }

        if (text == "/maxR3config") {
      modoR3 = CONFIG;
      modoMenu = CONFIG;
      R3config = 6;
      bot.sendMessage(chat_id, "Ingrese valor max R3: ");
    }
    if (R3config == 6) {
      maxR3 = text.toFloat();

      if (maxR3 > 0) {
        Serial.print("Valor max R3: ");
        Serial.println(maxR3);
        bot.sendMessage(chat_id, "Valor max R2 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R3config = 0;
      }
    }






    String modoRieg = "MODO RIEGO: \n";
    modoRieg += "/DiasRiegoInfo\n";
    modoRieg += "/LunesRiego\n";
    modoRieg += "/LunesNoRiego\n";
    modoRieg += "/MartesRiego\n";
    modoRieg += "/MartesNoRiego\n";
    modoRieg += "/MiercolesRiego\n";
    modoRieg += "/MiercolesNoRiego\n";
    modoRieg += "/JuevesRiego\n";
    modoRieg += "/JuevesNoRiego\n";
    modoRieg += "/ViernesRiego\n";
    modoRieg += "/ViernesNoRiego\n";
    modoRieg += "/SabadoRiego\n";
    modoRieg += "/SabadoNoRiego\n";
    modoRieg += "/DomingoRiego\n";
    modoRieg += "/DomingoNoRiego\n";

    //bot.sendMessage(chat_id, modoConf, "Markdown");

    if (text == "/DiasRiego") {

      bot.sendMessage(chat_id, modoRieg, "Markdown");
    }
    int d;
    if (text == "/DiasRiegoInfo") {
      for (d = 0; d < 7; d++) {
        if (diasRiego[d] == 1) {
          bot.sendMessage(chat_id, "Riego dia:  " + String(d) + ".");
        }
      }
    }

    if (text == "/LunesRiego") {
      diasRiego[1] = 1;
      bot.sendMessage(chat_id, "Lunes configurado: Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/LunesNoRiego") {
      diasRiego[1] = 0;
      bot.sendMessage(chat_id, "Lunes configurado: No Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/MartesRiego") {
      diasRiego[2] = 1;
      bot.sendMessage(chat_id, "Martes configurado: Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/MartesNoRiego") {
      diasRiego[2] = 0;
      bot.sendMessage(chat_id, "Martes configurado: No Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/MiercolesRiego") {
      diasRiego[3] = 1;
      bot.sendMessage(chat_id, "Miercoles configurado: Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/MiercolesNoRiego") {
      diasRiego[3] = 0;
      bot.sendMessage(chat_id, "Miercoles configurado: No Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/JuevesRiego") {
      diasRiego[4] = 1;
      bot.sendMessage(chat_id, "Jueves configurado: Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/JuevesNoRiego") {
      diasRiego[4] = 0;
      bot.sendMessage(chat_id, "Jueves configurado: No Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/ViernesRiego") {
      diasRiego[5] = 1;
      bot.sendMessage(chat_id, "Viernes configurado: Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/ViernesNoRiego") {
      diasRiego[5] = 0;
      bot.sendMessage(chat_id, "Viernes configurado: No Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/SabadoRiego") {
      diasRiego[6] = 1;
      bot.sendMessage(chat_id, "Sabado configurado: Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/SabadoNoRiego") {
      diasRiego[6] = 0;
      bot.sendMessage(chat_id, "Sabado configurado: No Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/DomingoRiego") {
      diasRiego[0] = 1;
      bot.sendMessage(chat_id, "Domingo configurado: Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }

    if (text == "/DomingoNoRiego") {
      diasRiego[0] = 0;
      bot.sendMessage(chat_id, "Domingo configurado: No Riego");
      bot.sendMessage(chat_id, modoRieg, "Markdown");
      Guardado_General();
    }
    // RELE 4 CONFIG

    if (text == "/horaOnR4config") {
      modoR4 = CONFIG;
      modoMenu = CONFIG;
      R4config = 1;
      bot.sendMessage(chat_id, "Ingrese hora de Encendido R4: ");
    }
    if (R4config == 1) {
      horaOnR4 = text.toInt();

      if (horaOnR4 > 0) {
        Serial.print("Hora encendido R4: ");
        Serial.println(horaOnR4);
        bot.sendMessage(chat_id, "Hora Encendido Rele 4 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R4config = 0;
      }
    }

    if (text == "/minOnR4config") {
      modoR4 = CONFIG;
      modoMenu = CONFIG;
      R4config = 2;
      bot.sendMessage(chat_id, "Ingrese minuto de Encendido R4: ");
    }
    if (R4config == 2) {
      minOnR4 = text.toInt();
      if (minOnR4 > 0) {
        Serial.print("Tiempo Minuto de encendido de Rele 4: ");
        Serial.println(minOnR4);
        bot.sendMessage(chat_id, "Minuto de encendido de R4 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R4config = 0;
      }
    }

    if (text == "/horaOffR4config") {
      modoR4 = CONFIG;
      modoMenu = CONFIG;
      R4config = 3;
      bot.sendMessage(chat_id, "Ingrese hora de apagado R4: ");
    }
    if (R4config == 3) {
      horaOffR4 = text.toInt();
      if (horaOffR4 > 0) {
        Serial.print("Tiempo hora de apagado Rele 4: ");
        Serial.println(horaOffR4);
        bot.sendMessage(chat_id, "Hora de apagado R4 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R4config = 0;
      }
    }

    if (text == "/minOffR4config") {
      modoR4 = CONFIG;
      modoMenu = CONFIG;
      R4config = 4;
      bot.sendMessage(chat_id, "Ingrese minuto de Apagado R4: ");
    }
    if (R4config == 4) {
      minOffR4 = text.toInt();
      if (minOffR4 > 0) {
        Serial.print("Tiempo minuto de apagado Rele 4: ");
        Serial.println(minOffR4);
        bot.sendMessage(chat_id, "Minuto de apagado R4 guardado");
        bot.sendMessage(chat_id, modoConf, "Markdown");
        Guardado_General();
        R4config = 0;
      }
    }

    //  MOSTRAR PARAMETROS

    if (text == "/infoconfig") {
      String infoConfig = "INFO CONFIG: \n";
      infoConfig += "Rele 1: \n";
      infoConfig += "minR1: " + String(minR1) + ".\n";
      infoConfig += "maxR1: " + String(maxR1) + ".\n";
      infoConfig += "paramR1: " + String(paramR1) + ".\n";
      infoConfig += "Hora de encendido: " + String(horaOnR1) + ":" + String(minOnR1) + "\n";
      infoConfig += "Hora de apagado: " + String(horaOffR1) + ":" + String(minOffR1) + "\n";      
      infoConfig += "modoR1: " + String(modoR1) + ".\n\n";
      infoConfig += "Rele 2: \n";
      infoConfig += "minR2: " + String(minR2) + ".\n";
      infoConfig += "maxR2: " + String(maxR2) + ".\n";
      infoConfig += "modoR2: " + String(modoR2) + ".\n\n";
      infoConfig += "Rele 2 (IR): \n";
      infoConfig += "minR2ir: " + String(minR2ir) + ".\n";
      infoConfig += "maxR2ir: " + String(maxR2ir) + ".\n";
      infoConfig += "modoR2ir: " + String(modoR2ir) + ".\n\n";
      infoConfig += "Rele 3: \n";
      infoConfig += "Hora de encendido: " + String(horaOnR3) + ":" + String(minOnR3) + "\n";
      infoConfig += "Hora de apagado: " + String(horaOffR3) + ":" + String(minOffR3) + "\n";
      infoConfig += "modoR3: " + String(modoR3) + ".\n\n";
      infoConfig += "Rele 4: \n";
      infoConfig += "Hora de encendido: " + String(horaOnR4) + ":" + String(minOnR4) + "\n";
      infoConfig += "Hora de apagado: " + String(horaOffR4) + ":" + String(minOffR4) + "\n";
      infoConfig += "modoR4: " + String(modoR4) + ".\n";
      bot.sendMessage(chat_id, infoConfig, "Markdown");
    }

    if (text == "/status" || modoMenu == STATUS) {
      modoMenu = STATUS;

      // Leer datos del sensor DHT
      //float temperature = dht.readTemperature();
      //float humidity = dht.readHumidity();

      sensors_event_t humidity, temp;
      aht.getEvent(&humidity, &temp);

      float temperature = (temp.temperature);
      float humedad = (humidity.relative_humidity);

      //int humedadS = analogRead(sensorHS); // Lee el valor analógico del sensor
      //int humedadSuelo = map(humedadS, 0, 4095, 0, 100);

      // Mapear el valor del sensor al rango de 0 a 100
     // int sensorValue = analogRead(sensorHS);
  //int humedadS = map(sensorValue, humedadMinima, humedadMaxima, 0, 100);

  // Limitar el valor de humedad a 0 y 100
  //if (humedadS < 0) humedadS = 0;
  //if (humedadS > 100) humedadS = 100;


     // sensors.requestTemperatures();
    //  float temperatureC = sensors.getTempCByIndex(0);

      DateTime now = rtc.now();
      int horaBot = now.hour();

      horaBot -= 3;
      if (horaBot < 0)
        horaBot = 24 + horaBot;

      int currentTimeBot = horaBot * 60 + now.minute();

      // Leer fecha y hora del RTC

      String dateTime = "Fecha y Hora: " + String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " " + horaBot + ":" + String(now.minute()) + ":" + String(now.second()) + "\n";

      String statusMessage = "Temperatura: " + String(temperature, 1) + " °C\n";
      statusMessage += "Humedad: " + String(humedad, 1) + " %\n";
      statusMessage += "DPV: " + String(DPV, 1) + " kPa\n";
      //statusMessage += "PH: " + String(PHval, 2);
      //statusMessage += "(" + String(PHvolt, 2) + " V)\n";
      //statusMessage += "Sensor HS: " + String(sensorValue);
//      statusMessage += "Humedad Suelo: " + String(humedadS) + " % (" + String(sensorValue) + ")\n";

      statusMessage += dateTime;  // Agrega la fecha y hora al mensaje
      bot.sendMessage(chat_id, statusMessage, "");
    }

    if (text == "/resetDruidaBot") {
      String resetMsg = "Reiniciando druida..\n";
      bot.sendMessage(chat_id, resetMsg, "Markdown");
      delay(2000);
      reset = 1;
    }

    if (text == "/enviarData") {
      String dataMsg = "Enviando data a Google Sheet\n";
      bot.sendMessage(chat_id, dataMsg, "Markdown");
      delay(500);
      sendDataToGoogleSheets();
    }
    delay(500);
  }
}


/*const int NUM_COMMANDS = 70; // Cambia este número según tus comandos
String commands[NUM_COMMANDS] = {
  "/start",//1
  "/config",//2
  "/manual",//3
  "/auto",//4
  "/status", //4
  "/infoconfig",//5 
  "/DiasRiego", //6
  "/resetDruidaBot",//7 
  "/enviarData",//8
  "/R1on", //9
  "/R1off", //10
  "/R1onTime", //11
  "/R2on", //12
  "/R2off", //13
  "/R2onTime", //14
  "/R2iron", //15
  "/R2iroff", //16
  "/R2ironTime",//17
  "/R3on", //18
  "/R3off", //19
  "/R3onTime", //20
  "/R4on", //21
  "/R4off", //22
  "/R4onTime", //23
  "/controlRemoto",//24
  "/R1auto", //25
  "/R1timer", //26
  "/R2auto", //27
  "/R2irauto", //28
  "/R3auto", //29
  "/R3autoParam",//30 
  "/R4auto",//31
  "/minR1config",//32 
  "/maxR1config", //33
  "/paramR1config", //34
  "/horaOnR1config", //35
  "/minOnR1config", //36
  "/horaOffR1config", //37
  "/minOffR1config",//38
  "/minR2config", //39
  "/maxR2config", //40
  "/paramR2config", //41
  "/minR2irconfig", //42
  "/maxR2irconfig", //43
  "/paramR2irconfig",//44
  "/horaOnR3config", //45
  "/minOnR3config", //46
  "/horaOffR3config", //47
  "/minOffR3config", //48
  "/minR3config", //49
  "/maxR3config",//50
  "/horaOnR4config", //51
  "/minOnR4config", //52
  "/horaOffR4config", //53
  "/minOffR4config",//54
  "/DiasRiegoInfo", //55
  "/LunesRiego", //56
  "/LunesNoRiego", //57
  "/MartesRiego", //58
  "/MartesNoRiego", //59
  "/MiercolesRiego", //60
  "/MiercolesNoRiego",//61
  "/JuevesRiego", //62
  "/JuevesNoRiego", //63
  "/ViernesRiego", //64
  "/ViernesNoRiego", //65
  "/SabadoRiego", //66
  "/SabadoNoRiego", //67
  "/DomingoRiego", //68
  "/DomingoNoRiego"//69
};

int commandCodes[NUM_COMMANDS] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9,
  10, 11, 12, 13, 14, 15, 16, 17, 18,
  19, 20, 21, 22, 23, 24, 25,
  26, 27, 28, 29, 30, 31, 32,
  33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45,
  46, 47, 48, 49, 50, 51,
  52, 53, 54, 55,
  56, 57, 58, 59, 60, 61, 62,
  63, 64, 65, 66, 67, 68, 69, 70
};

int getCommandCode(const String& command) {
  for (int i = 0; i < NUM_COMMANDS; i++) {
    if (command == commands[i]) {
      return commandCodes[i];
    }
  }
  return -1; // Comando no reconocido
}*/