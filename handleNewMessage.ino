// Proyecto: Druida BOT de DataDruida
// Autor: Bryan Murphy
// Año: 2025
// Licencia: MIT

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
    bot.sendMessage(chat_id, welcome, "Markdown");

    // Crear nuevo menú principal con los 3 botones principales
    String keyboardJson = "[[\"STATUS\"], [\"CONTROL\"], [\"CONFIG\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "MENU PRINCIPAL:", "", keyboardJson, true);
    delay(500);
}



    //MODO CONTROL

    if (text == "CONTROL") {
    modoMenu = AUTO;

    // Crear botones dinámicos basados en los nombres de los relés
    String controlKeyboardJson = 
        "[[\"Controlar " + getRelayName(R1name) + "\"], " +
        "[\"Controlar " + getRelayName(R2name) + "\"], " +
        "[\"Controlar " + getRelayName(R3name) + "\"], " +
        "[\"Controlar " + getRelayName(R4name) + "\"], " +
        "[\"Menu Principal\"]]";

    bot.sendMessageWithReplyKeyboard(chat_id, "Seleccione el dispositivo que desea controlar:", "", controlKeyboardJson, true);
}

if (text == "Menu Control") {
    String controlKeyboardJson = 
        "[[\"Controlar " + getRelayName(R1name) + "\"], " +
        "[\"Controlar " + getRelayName(R2name) + "\"], " +
        "[\"Controlar " + getRelayName(R3name) + "\"], " +
        "[\"Controlar " + getRelayName(R4name) + "\"], " +
        "[\"Menu Principal\"]]";

    bot.sendMessageWithReplyKeyboard(chat_id, "Volviendo al menú CONTROL:", "", controlKeyboardJson, true);
}

// Submenú para R1
if (text == "Controlar " + getRelayName(R1name)) {
    String relayControlKeyboardJson = 
        "[[\"Encender " + getRelayName(R1name) + "\", \"Apagar " + getRelayName(R1name) + "\"], " +
        "[\"Automatizar " + getRelayName(R1name) + "\"], " +
        "[\"Menu Control\"], [\"Menu Principal\"]]";
    
    bot.sendMessageWithReplyKeyboard(chat_id, "Controlando " + getRelayName(R1name) + ":", "", relayControlKeyboardJson, true);
}

// Submenú para R2
if (text == "Controlar " + getRelayName(R2name)) {
    String relayControlKeyboardJson = 
        "[[\"Encender " + getRelayName(R2name) + "\", \"Apagar " + getRelayName(R2name) + "\"], " +
        "[\"Automatizar " + getRelayName(R2name) + "\"], " +
        "[\"Menu Control\"], [\"Menu Principal\"]]";
    
    bot.sendMessageWithReplyKeyboard(chat_id, "Controlando " + getRelayName(R2name) + ":", "", relayControlKeyboardJson, true);
}

// Submenú para R3
if (text == "Controlar " + getRelayName(R3name)) {
    String relayControlKeyboardJson = 
        "[[\"Encender " + getRelayName(R3name) + "\", \"Apagar " + getRelayName(R3name) + "\"], " +
        "[\"Automatizar " + getRelayName(R3name) + "\"], " +
        "[\"Encender " + getRelayName(R3name) + " (segundos)\"], " + // Botón adicional
        "[\"Menu Control\"], [\"Menu Principal\"]]";
    
    bot.sendMessageWithReplyKeyboard(chat_id, "Controlando " + getRelayName(R3name) + ":", "", relayControlKeyboardJson, true);
}


// Submenú para R4
if (text == "Controlar " + getRelayName(R4name)) {
    String relayControlKeyboardJson = 
        "[[\"Encender " + getRelayName(R4name) + "\", \"Apagar " + getRelayName(R4name) + "\"], " +
        "[\"Automatizar " + getRelayName(R4name) + "\"], " +
        "[\"Menu Control\"], [\"Menu Principal\"]]";
    
    bot.sendMessageWithReplyKeyboard(chat_id, "Controlando " + getRelayName(R4name) + ":", "", relayControlKeyboardJson, true);
}




// CONTROL MANUAL Y AUTOMÁTICO

// Encender R1
if (text == "Encender " + getRelayName(R1name)) {
    modoR1 = MANUAL;
    estadoR1 = 1;
    bot.sendMessage(chat_id, getRelayName(R1name) + " está encendido.", "");
    Guardado_General();
    delay(500);
}

// Apagar R1
if (text == "Apagar " + getRelayName(R1name)) {
    modoR1 = MANUAL;
    estadoR1 = 0;
    bot.sendMessage(chat_id, getRelayName(R1name) + " está apagado.", "");
    Guardado_General();
    delay(500);
}

// Automatizar R1
if (text == "Automatizar " + getRelayName(R1name)) {
    modoR1 = AUTO;
    bot.sendMessage(chat_id, getRelayName(R1name) + " está en modo automático.", "");
    Guardado_General();
    delay(500);
}

// Repetir para R2
if (text == "Encender " + getRelayName(R2name)) {
    modoR2 = MANUAL;
    estadoR2 = 1;
    bot.sendMessage(chat_id, getRelayName(R2name) + " está encendido.", "");
    Guardado_General();
    delay(500);
}

if (text == "Apagar " + getRelayName(R2name)) {
    modoR2 = MANUAL;
    estadoR2 = 0;
    bot.sendMessage(chat_id, getRelayName(R2name) + " está apagado.", "");
    Guardado_General();
    delay(500);
}

if (text == "Automatizar " + getRelayName(R2name)) {
    modoR2 = AUTO;
    bot.sendMessage(chat_id, getRelayName(R2name) + " está en modo automático.", "");
    Guardado_General();
    delay(500);
}

// Repetir para R3
if (text == "Encender " + getRelayName(R3name)) {
    modoR3 = MANUAL;
    estadoR3 = 1;
    bot.sendMessage(chat_id, getRelayName(R3name) + " está encendido.", "");
    Guardado_General();
    delay(500);
}

if (text == "Apagar " + getRelayName(R3name)) {
    modoR3 = MANUAL;
    estadoR3 = 0;
    bot.sendMessage(chat_id, getRelayName(R3name) + " está apagado.", "");
    Guardado_General();
    delay(500);
}

if (text == "Automatizar " + getRelayName(R3name)) {
    modoR3 = AUTO;
    bot.sendMessage(chat_id, getRelayName(R3name) + " está en modo automático.", "");
    Guardado_General();
    delay(500);
}

// Repetir para R4
if (text == "Encender " + getRelayName(R4name)) {
    modoR4 = MANUAL;
    estadoR4 = 1;
    bot.sendMessage(chat_id, getRelayName(R4name) + " está encendido.", "");
    Guardado_General();
    delay(500);
}

if (text == "Apagar " + getRelayName(R4name)) {
    modoR4 = MANUAL;
    estadoR4 = 0;
    bot.sendMessage(chat_id, getRelayName(R4name) + " está apagado.", "");
    Guardado_General();
    delay(500);
}

if (text == "Automatizar " + getRelayName(R4name)) {
    modoR4 = AUTO;
    bot.sendMessage(chat_id, getRelayName(R4name) + " está en modo automático.", "");
    Guardado_General();
    delay(500);
}


// Opción para encender R3 por un tiempo en segundos
if (text == "Encender " + getRelayName(R3name) + " (segundos)") {
    esperandoTiempoR3 = true; // Establece que estamos esperando el tiempo de encendido para R3
    bot.sendMessage(chat_id, "¿Por cuánto tiempo (en segundos) quieres encender " + getRelayName(R3name) + "?", "");
    delay(500);
}

// Procesar el tiempo ingresado por el usuario
if (esperandoTiempoR3 && text != "Encender " + getRelayName(R3name) + " (segundos)") {
    tiempoR3 = text.toInt(); // Convierte el texto ingresado en un número entero
    if (tiempoR3 > 0) { // Si el valor ingresado es válido
        esperandoTiempoR3 = false; // Resetea la variable
        encenderRele3PorTiempo(tiempoR3); // Enciende el relé 3 por el tiempo indicado
        bot.sendMessage(chat_id, getRelayName(R3name) + " estará encendido por " + String(tiempoR3) + " segundos.", "");
    } else {
        bot.sendMessage(chat_id, "Por favor ingresa un valor válido en segundos.", "");
    }
    delay(500);
}



if (text == "CONFIG") {
    // Crear botones dinámicos para la configuración de cada relé
    String configKeyboardJson = 
        "[[\"Configurar " + getRelayName(R1name) + "\"], " +
        "[\"Configurar " + getRelayName(R2name) + "\"], " +
        "[\"Configurar " + getRelayName(R3name) + "\"], " +
        "[\"Configurar " + getRelayName(R4name) + "\"], " +
        "[\"INFO CONFIG\"], " +
        "[\"RESET DRUIDA\"], " +
        "[\"WIFI ACCES POINT\"], " +
        "[\"Menu Principal\"]]";

    bot.sendMessageWithReplyKeyboard(chat_id, "Seleccione una opción para configurar:", "", configKeyboardJson, true);
}




// Menú de configuración para R1
if (text == "Configurar " + getRelayName(R1name)) {
    String r1KeyboardJson = "[[\"Min " + getRelayName(R1name) + "\", \"Max " + getRelayName(R1name) + "\"], " +
                            "[\"Hora On " + getRelayName(R1name) + "\", \"Hora Off " + getRelayName(R1name) + "\"], " +
                            //"[\"Parametro " + getRelayName(R1name) + "\", \"Config " + getRelayName(R1name) + " Name\"], " +
                            "[\"Menu CONFIG\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "Opciones de configuración para " + getRelayName(R1name) + ":", "", r1KeyboardJson, true);
}

// Menú de configuración para R2
if (text == "Configurar " + getRelayName(R2name)) {
    String r2KeyboardJson = "[[\"Min " + getRelayName(R2name) + "\", \"Max " + getRelayName(R2name) + "\"], " +
                            "[\"Parametro " + getRelayName(R2name) + "\"], " +
                            //"[\"Config " + getRelayName(R2name) + " Name\"], " +
                            "[\"Menu CONFIG\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "Opciones de configuración para " + getRelayName(R2name) + ":", "", r2KeyboardJson, true);
}

// Menú de configuración para R3
if (text == "Configurar " + getRelayName(R3name)) {
    String r3KeyboardJson = "[[\"Hora On " + getRelayName(R3name) + "\", \"Hora Off " + getRelayName(R3name) + "\"], " +
                            "[\"Dias de Riego\", \"Duración de Riego\"], " +
                            "[\"Intervalo de Riego\"], " +
                            //"[\"Config " + getRelayName(R3name) + " Name\",
                             "[\"Menu CONFIG\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "Opciones de configuración para " + getRelayName(R3name) + ":", "", r3KeyboardJson, true);
}

// Menú de configuración para R4
if (text == "Configurar " + getRelayName(R4name)) {
    String r4KeyboardJson = "[[\"Hora On " + getRelayName(R4name) + "\", \"Hora Off " + getRelayName(R4name) + "\"], " +
                            //"[\"Hora Amanecer " + getRelayName(R4name) + "\", \"Hora Atardecer " + getRelayName(R4name) + "\"], " +
                            //"[\"Config " + getRelayName(R4name) + " Name\", 
                            "[\"Menu CONFIG\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "Opciones de configuración para " + getRelayName(R4name) + ":", "", r4KeyboardJson, true);
}

// Volver al menú CONFIG
if (text == "Menu CONFIG") {
    String configKeyboardJson = 
        "[[\"Configurar " + getRelayName(R1name) + "\"], " +
        "[\"Configurar " + getRelayName(R2name) + "\"], " +
        "[\"Configurar " + getRelayName(R3name) + "\"], " +
        "[\"Configurar " + getRelayName(R4name) + "\"], " +
        "[\"INFO CONFIG\"], " +
        "[\"RESET DRUIDA\"], " +
        "[\"MODO LOCAL\"], " +
        "[\"Menu Principal\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "Seleccione una opción para configurar:", "", configKeyboardJson, true);
}

if (text == "MODO LOCAL"){
  modoWiFi = 0;
  Guardado_General();
  String resetMsg = "Reiniciando druida..\n";
  bot.sendMessage(chat_id, resetMsg, "Markdown");
  delay(2000);
  reset = 1;


}


    // Configurar el valor mínimo
if (text == "Min " + getRelayName(R1name)) {
    modoR1 = CONFIG;
    modoMenu = CONFIG;
    R1config = 1;
    bot.sendMessage(chat_id, "Ingrese valor Min " + getRelayName(R1name) + ":");
}
if (R1config == 1) {
    minR1 = text.toFloat();
    if (minR1 > 0 && minR1 < 100) {
        Serial.print("Valor min " + getRelayName(R1name) + ": ");
        Serial.println(minR1);
        bot.sendMessage(chat_id, "Valor min " + getRelayName(R1name) + " guardado");
        Guardado_General();
        R1config = 0;
    }
}

// Configurar el valor máximo
if (text == "Max " + getRelayName(R1name)) {
    modoR1 = CONFIG;
    modoMenu = CONFIG;
    R1config = 2;
    bot.sendMessage(chat_id, "Ingrese valor Max " + getRelayName(R1name) + ":");
}
if (R1config == 2) {
    maxR1 = text.toFloat();
    if (maxR1 > 0) {
        Serial.print("Valor max " + getRelayName(R1name) + ": ");
        Serial.println(maxR1);
        bot.sendMessage(chat_id, "Valor max " + getRelayName(R1name) + " guardado");
        Guardado_General();
        R1config = 0;
    }
}

// Configurar el parámetro
if (text == "Parametro " + getRelayName(R1name)) {
    modoR1 = CONFIG;
    modoMenu = CONFIG;
    R1config = 3;
    bot.sendMessage(chat_id, "Ingrese parámetro para " + getRelayName(R1name) + ":\n1- Humedad\n2- Temperatura\n3- DPV.");
}
if (R1config == 3) {
    paramR1 = text.toInt();
    if (paramR1 > 0) {
        Serial.print("Parametro " + getRelayName(R1name) + ": ");
        Serial.println(paramR1);
        bot.sendMessage(chat_id, "Valor parametro " + getRelayName(R1name) + " guardado");
        Guardado_General();
        R1config = 0;
    }
}


// Configurar Hora On y Hora Off (sin cambios)
if (text == "Hora On " + getRelayName(R1name)) {
    modoR1 = CONFIG;
    modoMenu = CONFIG;
    R1config = 4;
    bot.sendMessage(chat_id, "Ingrese Hora On " + getRelayName(R1name) + " en formato HH:MM (por ejemplo, 08:30):");
} else if (R1config == 4) {
    int sep = text.indexOf(':');
    if (sep != -1) {
        horaOnR1 = text.substring(0, sep).toInt();
        minOnR1 = text.substring(sep + 1).toInt();
        if (horaOnR1 >= 0 && horaOnR1 < 24 && minOnR1 >= 0 && minOnR1 < 60) {
            Serial.print("Hora On " + getRelayName(R1name) + ": ");
            Serial.print(horaOnR1);
            Serial.print(":");
            Serial.println(minOnR1);
            bot.sendMessage(chat_id, "Hora On " + getRelayName(R1name) + " guardada correctamente");
            Guardado_General();
            R1config = 0;
        } else {
            bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
        }
    } else {
        bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
    }
}

if (text == "Hora Off " + getRelayName(R1name)) {
    modoR1 = CONFIG;
    modoMenu = CONFIG;
    R1config = 5;
    bot.sendMessage(chat_id, "Ingrese Hora Off " + getRelayName(R1name) + " en formato HH:MM (por ejemplo, 18:45):");
} else if (R1config == 5) {
    int sep = text.indexOf(':'); // Buscamos el separador ':'
    if (sep != -1) {
        horaOffR1 = text.substring(0, sep).toInt(); // Obtenemos la hora
        minOffR1 = text.substring(sep + 1).toInt(); // Obtenemos los minutos
        if (horaOffR1 >= 0 && horaOffR1 < 24 && minOffR1 >= 0 && minOffR1 < 60) {
            Serial.print("Hora Off " + getRelayName(R1name) + ": ");
            Serial.print(horaOffR1);
            Serial.print(":");
            Serial.println(minOffR1);
            bot.sendMessage(chat_id, "Hora Off " + getRelayName(R1name) + " guardada correctamente");
            Guardado_General(); // Guardar los valores configurados
            R1config = 0; // Reiniciamos la configuración
        } else {
            bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
        }
    } else {
        bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
    }
}


/// RELE 3 CONFIG

// Configurar Duración de Riego
if (text == "Duración de Riego") {
    modoR3 = CONFIG;
    modoMenu = CONFIG;
    R3config = 3;
    bot.sendMessage(chat_id, "Ingrese la duración del riego (en segundos):");
}
if (R3config == 3) {
    tiempoRiego = text.toInt();
    if (tiempoRiego > 0) {
        Serial.print("Duración de riego: ");
        Serial.println(tiempoRiego);
        bot.sendMessage(chat_id, "Duración del riego guardada correctamente");
        Guardado_General();
        R3config = 0; // Reiniciar configuración
    } else {
        bot.sendMessage(chat_id, "Error: El valor debe ser un número mayor a 0.");
    }
}

// Configurar Intervalo de Riego
if (text == "Intervalo de Riego") {
    modoR3 = CONFIG;
    modoMenu = CONFIG;
    R3config = 4;
    bot.sendMessage(chat_id, "Ingrese el intervalo entre riegos (en segundos):");
}
if (R3config == 4) {
    tiempoNoRiego = text.toInt();
    if (tiempoNoRiego > 0) {
        Serial.print("Intervalo de riego: ");
        Serial.println(tiempoNoRiego);
        bot.sendMessage(chat_id, "Intervalo de riego guardado correctamente");
        Guardado_General();
        R3config = 0; // Reiniciar configuración
    } else {
        bot.sendMessage(chat_id, "Error: El valor debe ser un número mayor a 0.");
    }
}

// Configurar Hora On
if (text == "Hora On " + getRelayName(R3name)) {
    modoR3 = CONFIG;
    modoMenu = CONFIG;
    R3config = 1;
    bot.sendMessage(chat_id, "Ingrese Hora de Encendido " + getRelayName(R3name) + " en formato HH:MM (por ejemplo, 08:30):");
} else if (R3config == 1) {
    int sep = text.indexOf(':');
    if (sep != -1) {
        horaOnR3 = text.substring(0, sep).toInt();
        minOnR3 = text.substring(sep + 1).toInt();
        if (horaOnR3 >= 0 && horaOnR3 < 24 && minOnR3 >= 0 && minOnR3 < 60) {
            Serial.print("Hora encendido " + getRelayName(R3name) + ": ");
            Serial.print(horaOnR3);
            Serial.print(":");
            Serial.println(minOnR3);
            bot.sendMessage(chat_id, "Hora de encendido " + getRelayName(R3name) + " guardada correctamente");
            Guardado_General();
            R3config = 0; // Reiniciar configuración
        } else {
            bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
        }
    } else {
        bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
    }
}

// Configurar Hora Off
if (text == "Hora Off " + getRelayName(R3name)) {
    modoR3 = CONFIG;
    modoMenu = CONFIG;
    R3config = 2;
    bot.sendMessage(chat_id, "Ingrese Hora de Apagado " + getRelayName(R3name) + " en formato HH:MM (por ejemplo, 18:45):");
} else if (R3config == 2) {
    int sep = text.indexOf(':');
    if (sep != -1) {
        horaOffR3 = text.substring(0, sep).toInt();
        minOffR3 = text.substring(sep + 1).toInt();
        if (horaOffR3 >= 0 && horaOffR3 < 24 && minOffR3 >= 0 && minOffR3 < 60) {
            Serial.print("Hora apagado " + getRelayName(R3name) + ": ");
            Serial.print(horaOffR3);
            Serial.print(":");
            Serial.println(minOffR3);
            bot.sendMessage(chat_id, "Hora de apagado " + getRelayName(R3name) + " guardada correctamente");
            Guardado_General();
            R3config = 0; // Reiniciar configuración
        } else {
            bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
        }
    } else {
        bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
    }
}





if (text == "Dias de Riego") {
    // Crear un teclado con botones para los días de riego, incluyendo "Riego todos los días"
    String riegoKeyboardJson = "[[\"Riego todos los dias\"],"
                               "[\"Lunes Riego\", \"Lunes No Riego\"], [\"Martes Riego\", \"Martes No Riego\"],"
                               "[\"Miercoles Riego\", \"Miercoles No Riego\"], [\"Jueves Riego\", \"Jueves No Riego\"],"
                               "[\"Viernes Riego\", \"Viernes No Riego\"], [\"Sabado Riego\", \"Sabado No Riego\"],"
                               "[\"Domingo Riego\", \"Domingo No Riego\"], [\"Menu Principal\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "Seleccione los días de riego:", "", riegoKeyboardJson, true);
}

if (text == "Riego todos los dias") {
    // Configurar todos los días para riego
    for (int i = 0; i < 7; i++) {
        diasRiego[i] = 1; // Asignar 1 a cada día
    }
    bot.sendMessage(chat_id, "Todos los días configurados: Riego", "");
    Guardado_General(); // Guardar configuración
}




    if (text == "Lunes Riego") {
      diasRiego[1] = 1;
      bot.sendMessage(chat_id, "Lunes configurado: Riego");
      Guardado_General();
    }

    if (text == "Lunes No Riego") {
      diasRiego[1] = 0;
      bot.sendMessage(chat_id, "Lunes configurado: No Riego");
      Guardado_General();
    }

    if (text == "Martes Riego") {
      diasRiego[2] = 1;
      bot.sendMessage(chat_id, "Martes configurado: Riego");
      Guardado_General();
    }

    if (text == "Martes No Riego") {
      diasRiego[2] = 0;
      bot.sendMessage(chat_id, "Martes configurado: No Riego");
      Guardado_General();
    }

    if (text == "Miercoles Riego") {
      diasRiego[3] = 1;
      bot.sendMessage(chat_id, "Miercoles configurado: Riego");
      Guardado_General();
    }

    if (text == "Miercoles No Riego") {
      diasRiego[3] = 0;
      bot.sendMessage(chat_id, "Miercoles configurado: No Riego");
      Guardado_General();
    }

    if (text == "Jueves Riego") {
      diasRiego[4] = 1;
      bot.sendMessage(chat_id, "Jueves configurado: Riego");
      Guardado_General();
    }

    if (text == "Jueves No Riego") {
      diasRiego[4] = 0;
      bot.sendMessage(chat_id, "Jueves configurado: No Riego");
      Guardado_General();
    }

    if (text == "Viernes Riego") {
      diasRiego[5] = 1;
      bot.sendMessage(chat_id, "Viernes configurado: Riego");
      Guardado_General();
    }

    if (text == "Viernes No Riego") {
      diasRiego[5] = 0;
      bot.sendMessage(chat_id, "Viernes configurado: No Riego");
      Guardado_General();
    }

    if (text == "Sabado Riego") {
      diasRiego[6] = 1;
      bot.sendMessage(chat_id, "Sabado configurado: Riego");
      Guardado_General();
    }

    if (text == "Sabado No Riego") {
      diasRiego[6] = 0;
      bot.sendMessage(chat_id, "Sabado configurado: No Riego");
      Guardado_General();
    }

    if (text == "Domingo Riego") {
      diasRiego[0] = 1;
      bot.sendMessage(chat_id, "Domingo configurado: Riego");
      Guardado_General();
    }

    if (text == "Domingo No Riego") {
      diasRiego[0] = 0;
      bot.sendMessage(chat_id, "Domingo configurado: No Riego");
      Guardado_General();
    }

   // RELE 4 CONFIG

// Menú dinámico de configuración para R4
if (text == getRelayName(R4name) + " config") {
    // Crear botones dinámicos para las opciones de R4
    String r4KeyboardJson = "[[\"Hora On " + getRelayName(R4name) + "\", \"Hora Off " + getRelayName(R4name) + "\"], "
                            "[\"Hora Amanecer " + getRelayName(R4name) + "\", \"Hora Atardecer " + getRelayName(R4name) + "\"], "
                            "[\"Config " + getRelayName(R4name) + " Name\", \"Menu Principal\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "Opciones de configuración para " + getRelayName(R4name) + ":", "", r4KeyboardJson, true);
}

// Configurar Hora On R4
if (text == "Hora On " + getRelayName(R4name)) {
    modoR4 = CONFIG;
    modoMenu = CONFIG;
    R4config = 1;
    bot.sendMessage(chat_id, "Ingrese Hora de Encendido " + getRelayName(R4name) + " en formato HH:MM (por ejemplo, 08:30):");
} else if (R4config == 1) {
    int sep = text.indexOf(':');
    if (sep != -1) {
        horaOnR4 = text.substring(0, sep).toInt();
        minOnR4 = text.substring(sep + 1).toInt();
        if (horaOnR4 >= 0 && horaOnR4 < 24 && minOnR4 >= 0 && minOnR4 < 60) {
            Serial.print("Hora encendido " + getRelayName(R4name) + ": ");
            Serial.print(horaOnR4);
            Serial.print(":");
            Serial.println(minOnR4);
            bot.sendMessage(chat_id, "Hora de encendido " + getRelayName(R4name) + " guardada correctamente");
            Guardado_General();
            R4config = 0; // Reseteamos la configuración

            } else {
        bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
    }
    }
}

// Configurar Hora Off R4
if (text == "Hora Off " + getRelayName(R4name)) {
    modoR4 = CONFIG;
    modoMenu = CONFIG;
    R4config = 2;
    bot.sendMessage(chat_id, "Ingrese Hora de Apagado " + getRelayName(R4name) + " en formato HH:MM (por ejemplo, 18:45):");
} else if (R4config == 2) {
    int sep = text.indexOf(':');
    if (sep != -1) {
        horaOffR4 = text.substring(0, sep).toInt();
        minOffR4 = text.substring(sep + 1).toInt();
        if (horaOffR4 >= 0 && horaOffR4 < 24 && minOffR4 >= 0 && minOffR4 < 60) {
            Serial.print("Hora apagado " + getRelayName(R4name) + ": ");
            Serial.print(horaOffR4);
            Serial.print(":");
            Serial.println(minOffR4);
            bot.sendMessage(chat_id, "Hora de apagado " + getRelayName(R4name) + " guardada correctamente");
            Guardado_General();
            R4config = 0; // Reseteamos la configuración
        } else {
            bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
        }
    } else {
        bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
    }
}

// Configurar Hora Amanecer R4
if (text == "Hora Amanecer " + getRelayName(R4name)) {
    modoR4 = CONFIG;
    modoMenu = CONFIG;
    R4config = 3;
    bot.sendMessage(chat_id, "Ingrese la hora del amanecer para " + getRelayName(R4name) + " en formato HH:MM (por ejemplo, 06:30):");
} else if (R4config == 3) {
    int sep = text.indexOf(':');
    if (sep != -1) {
        int hora = text.substring(0, sep).toInt();
        int minuto = text.substring(sep + 1).toInt();
        if (hora >= 0 && hora < 24 && minuto >= 0 && minuto < 60) {
            horaAmanecer = hora * 60 + minuto; // Convertimos a minutos totales
            Serial.print("Hora amanecer para " + getRelayName(R4name) + " (en minutos totales): ");
            Serial.println(horaAmanecer);
            bot.sendMessage(chat_id, "Hora del amanecer guardada correctamente");
            Guardado_General();
            R4config = 0; // Reseteamos la configuración
        } else {
            bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
        }
    } else {
        bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
    }
}

// Configurar Hora Atardecer R4
if (text == "Hora Atardecer " + getRelayName(R4name)) {
    modoR4 = CONFIG;
    modoMenu = CONFIG;
    R4config = 4;
    bot.sendMessage(chat_id, "Ingrese la hora del atardecer para " + getRelayName(R4name) + " en formato HH:MM (por ejemplo, 18:30):");
} else if (R4config == 4) {
    int sep = text.indexOf(':');
    if (sep != -1) {
        int hora = text.substring(0, sep).toInt();
        int minuto = text.substring(sep + 1).toInt();
        if (hora >= 0 && hora < 24 && minuto >= 0 && minuto < 60) {
            horaAtardecer = hora * 60 + minuto; // Convertimos a minutos totales
            Serial.print("Hora atardecer para " + getRelayName(R4name) + " (en minutos totales): ");
            Serial.println(horaAtardecer);
            bot.sendMessage(chat_id, "Hora del atardecer guardada correctamente");
            Guardado_General();
            R4config = 0; // Reseteamos la configuración
        } else {
            bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
        }
    } else {
        bot.sendMessage(chat_id, "Error: Formato incorrecto. Ingrese en formato HH:MM.");
    }
}

// Configurar el nombre dinámico de R4
if (text == "Config " + getRelayName(R4name) + " Name") {
    String options = "Ingrese un número para el nuevo nombre de " + getRelayName(R4name) + ":\n"
                     "1 - Extraccion\n"
                     "2 - Intraccion\n"
                     "3 - Humidificador\n"
                     "4 - Caloventor\n"
                     "5 - Luz\n"
                     "6 - Riego";
    bot.sendMessage(chat_id, options);
    R4config = 5; // Cambiar el estado del menú para esperar un valor
}
if (R4config == 5) {
    int newIndex = text.toInt();
    if (newIndex >= 1 && newIndex <= 6) {
        R4name = newIndex - 1; // Actualizar el índice del nombre
        bot.sendMessage(chat_id, "Nombre cambiado a: " + getRelayName(R4name));
        Guardado_General();
        R4config = 0;
    } else {
        bot.sendMessage(chat_id, "Número inválido. Intente de nuevo.");
    }
}




// MOSTRAR PARAMETROS

if (text == "INFO CONFIG") {
    // Crear botones para mostrar la información de cada relé con nombres dinámicos
    String infoKeyboardJson = "[[\"" + getRelayName(R1name) + " Info\", \"" + getRelayName(R2name) + " Info\"], "
                              "[\"" + getRelayName(R3name) + " Info\", \"" + getRelayName(R4name) + " Info\"], "
                              "[\"Menu Principal\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "Seleccione un relé para ver la información:", "", infoKeyboardJson, true);
}

// Información de R1
if (text == getRelayName(R1name) + " Info") {
    String infoR1 = getRelayName(R1name) + ": \n";
    infoR1 += "Límite Inferior: " + String(minR1) + ".\n";
    infoR1 += "Límite Superior: " + String(maxR1) + ".\n";
    infoR1 += "Parametro: " + convertirParametro(paramR1) + ".\n";
    infoR1 += "Hora de encendido: " + formatoHora(horaOnR1, minOnR1) + "\n";
    infoR1 += "Hora de apagado: " + formatoHora(horaOffR1, minOffR1) + "\n";
    infoR1 += "Modo: " + convertirModo(modoR1) + ".\n";
    bot.sendMessage(chat_id, infoR1, "Markdown");
}

// Información de R2
if (text == getRelayName(R2name) + " Info") {
    String infoR2 = getRelayName(R2name) + ": \n";
    infoR2 += "Límite Inferior: " + String(minR2) + ".\n";
    infoR2 += "Límite Superior: " + String(maxR2) + ".\n";
    infoR2 += "Parametro: " + convertirParametro(paramR2) + ".\n";
    infoR2 += "Modo: " + convertirModo(modoR2) + ".\n";
    infoR2 += getRelayName(R2name) + " (IR): \n";
    //infoR2 += "Límite Inferior: " + String(minR2ir) + ".\n";
    //infoR2 += "Límite Superior: " + String(maxR2ir) + ".\n";
    //infoR2 += "Parametro: " + convertirParametro(paramR2ir) + ".\n";
    //infoR2 += "Modo: " + convertirModo(modoR2ir) + ".\n";
    bot.sendMessage(chat_id, infoR2, "Markdown");
}

// Información de R3
if (text == getRelayName(R3name) + " Info") {
    String infoR3 = getRelayName(R3name) + ": \n";
    infoR3 += "Hora de encendido: " + formatoHora(horaOnR3, minOnR3) + "\n";
    infoR3 += "Hora de apagado: " + formatoHora(horaOffR3, minOffR3) + "\n";
    infoR3 += "Duración de riego: " + String(tiempoRiego) + " segundos.\n";
    infoR3 += "Intervalo de riego: " + String(tiempoNoRiego) + " segundos.\n";
    //infoR3 += "Cantidad de riegos: " + String(cantidadRiegos) + ".\n";
    infoR3 += "Modo: " + convertirModo(modoR3) + ".\n";

    // Agregar información de los días de riego
    String diasRiegoInfo = "Días de riego:\n";
    bool hayRiego = false;

    for (int d = 0; d < 7; d++) {
        if (diasRiego[d] == 1) {
            diasRiegoInfo += "-" + convertirDia(d) + ".\n";
            hayRiego = true;
        }
    }

    if (hayRiego) {
        infoR3 += diasRiegoInfo + "\n";
    } else {
        infoR3 += "No hay días de riego configurados.\n";
    }

    bot.sendMessage(chat_id, infoR3, "Markdown");
}

// Información de R4
if (text == getRelayName(R4name) + " Info") {
    String infoR4 = getRelayName(R4name) + ": \n";
    infoR4 += "Hora de encendido: " + formatoHora(horaOnR4, minOnR4) + "\n";
    infoR4 += "Hora de apagado: " + formatoHora(horaOffR4, minOffR4) + "\n";
    infoR4 += "Hora de amanecer: " + formatoHora(horaAmanecer / 60, horaAmanecer % 60) + "\n";
    infoR4 += "Hora de atardecer: " + formatoHora(horaAtardecer / 60, horaAtardecer % 60) + "\n";
    infoR4 += "Modo: " + convertirModo(modoR4) + ".\n";
    bot.sendMessage(chat_id, infoR4, "Markdown");
}

// Volver al menú principal
if (text == "Menu Principal") {
    String mainKeyboardJson = "[[\"STATUS\"], [\"CONTROL\"], [\"CONFIG\"]]";
    bot.sendMessageWithReplyKeyboard(chat_id, "Has vuelto al menú principal. Selecciona una opción:", "", mainKeyboardJson, true);
}



if (text == "STATUS" ) {
    modoMenu = STATUS;

    // Leer datos del sensor DHT
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);

    float temperature = (temp.temperature);
    float humedad = (humidity.relative_humidity);

    requestSensorData();

    DateTime now = rtc.now();
    int horaBot = now.hour();

    horaBot -= 3;
    if (horaBot < 0)
        horaBot = 24 + horaBot;

    int currentTimeBot = horaBot * 60 + now.minute();

    // Leer fecha y hora del RTC
    String dateTime = "📅 Fecha y Hora: " + String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " " + horaBot + ":" + String(now.minute()) + ":" + String(now.second()) + "\n";

    String statusMessage = "🌡️ Temperatura: " + String(temperature, 1) + " °C\n";
    statusMessage += "💧 Humedad: " + String(humedad, 1) + " %\n";
    statusMessage += "🌬️ DPV: " + String(DPV, 1) + " hPa\n";
    statusMessage += "🌱 Humedad 1: " + String(sensor1Value) + " %\n";
    statusMessage += "🌱 Humedad 2: " + String(sensor2Value) + " %\n";
    statusMessage += "🌱 Humedad 3: " + String(sensor3Value) + " %\n";
    statusMessage += "🧪 pH: " + String(sensorPH, 2) + "\n";


    statusMessage += dateTime;  // Agrega la fecha y hora al mensaje
    bot.sendMessage(chat_id, statusMessage, "");
}


    if (text == "RESET DRUIDA") {
      String resetMsg = "Reiniciando druida..\n";
      bot.sendMessage(chat_id, resetMsg, "Markdown");
      delay(2000);
      reset = 1;
    }

    if (text == "WIFI ACCES POINT") {
      String resetMsg = "Reiniciando Druida.. (Modo AP)\n";
      bot.sendMessage(chat_id, resetMsg, "Markdown");
      delay(1000);
      modoWiFi = 0;
      Guardado_General();
      reset = 1;

    }

    if (text == "ENVIAR DATA GOOGLE") {
      String dataMsg = "Enviando data a Google Sheet\n";
      bot.sendMessage(chat_id, dataMsg, "Markdown");
      delay(500);
      sendDataToGoogleSheets();
    }


  }
  delay(500);
}
