#include <Wire.h>               // Librería para comunicación I2C
#include <LiquidCrystal_I2C.h>  // Librería para el LCD con adaptador I2C
#include <RTClib.h>             // Librería para el módulo RTC    
#include <Servo.h>              // Librería para el control del servomotor
#include <EEPROM.h>             // Librería para el acceso a la memoria EEPROM


/*
  ============================================================
  PASTILLERO PARA PERSONAS MAYORES
  Estructura general del programa
  ============================================================

  Hardware asumido:
  - Arduino Nano clásico
  - RTC DS3231
  - LCD 1602 con adaptador I2C
  - Joystick analógico con botón
  - Servomotor
  - 74HC595 para 8 LEDs
*/

// ============================================================
// CONFIGURACIÓN GENERAL
// ============================================================

  constexpr unsigned long INTERVALO_LECTURA_RTC_MS = 250UL;
  constexpr unsigned long INTERVALO_SERVO_MS = 400UL;
  constexpr unsigned long DURACION_MOSTRAR_DOSIS = 120000UL;

// ============================================================
// CONFIGURACIÓN DE PINES
// ============================================================
// Joystick
  constexpr uint8_t PIN_X = A0;
  constexpr uint8_t PIN_Y = A1;
  constexpr uint8_t BOTTON = 2;

//Servomotor
  constexpr uint8_t PIN_SERVO = 9;

// Registro 74HC595 para LEDs

// ============================================================
// CONFIGURACIÓN DEL LCD
// ============================================================
  //Configuracion de LCD
    constexpr uint8_t DIRECCION_LCD = 0x27;
    constexpr uint8_t COLUMNAS_LCD = 16;
    constexpr uint8_t FILAS_LCD = 2;

    LiquidCrystal_I2C lcd(
      DIRECCION_LCD,
      COLUMNAS_LCD,
      FILAS_LCD
    );


// ============================================================
// OBJETOS DE HARDWARE
// ============================================================
  Servo servoCampana;
  RTC_DS3231 rtc;

// ============================================================
// ESTADOS DEL SISTEMA
// ============================================================
  enum class EstadoSistema : uint8_t {
    INICIANDO,
    NORMAL,
    MENU_PRINCIPAL,
    ALARMA_ACTIVA,
    ERROR_RTC

  };

  EstadoSistema estadoActual = EstadoSistema::INICIANDO;
 

// ============================================================
// EVENTOS DEL JOYSTICK
// ============================================================

  enum class EventosJoystick : uint8_t {
    NINGUNO,
    ARRIBA,
    ABAJO,
    IZQUIERDA,
    DERECHA,
    BOTON

  };

// ============================================================
// CONFIGURACION DEL JOYSTICK
// ============================================================
  // Límites para detectar una dirección
  constexpr int JOYSTICK_LIMITE_BAJO = 400;
  constexpr int JOYSTICK_LIMITE_ALTO = 600;

  // Zona considerada como posición central
  constexpr int JOYSTICK_CENTRO_BAJO = 400;
  constexpr int JOYSTICK_CENTRO_ALTO = 600;

  // Tiempo de antirrebote del botón
  constexpr unsigned long ANTIRREBOTE_BOTON_MS = 40;


// ============================================================
// DÍAS DE LA SEMANA
// ============================================================

/*
  Cada bit representa un día.

  Bit 0: domingo
  Bit 1: lunes
  Bit 2: martes
  Bit 3: miércoles
  Bit 4: jueves
  Bit 5: viernes
  Bit 6: sábado

  Ejemplo:
  0b01111111 = todos los días
*/



// ============================================================
// ESTRUCTURA DE UNA ALARMA
// ============================================================



// ============================================================
// VARIABLES GLOBALES DEL SISTEMA
// ============================================================
  
  DateTime fechaHoraActual;

  bool rtcDisponible = false;
  bool pantallaDebeActualizarse = true;
  
  int8_t indiceAlarmaActiva = -1;
  uint8_t opcionMenuPrincipal = 0;

  unsigned long momentoEntradaEstado = 0;
  unsigned long ultimaLecturaRTC = 0;
  unsigned long ultimoMovimientoServo = 0;

  bool servoEnPosicionDerecha = false;


// ============================================================
// DECLARACIÓN DE FUNCIONES
// ============================================================
  //Inicializacion
    void inicializarPines();
    void inicializarPantalla();
    bool inicializarRTC();
    void inicializarServo();

  //Control geeral
    void cambiarEstado(EstadoSistema nuevoEstado);
    void actualizarEstado(EventosJoystick evento);
    void actualizarReloj();
    void ErrorRTC(EventosJoystick evento);

  //Estados
    void EstadoNormal(EventosJoystick evento);
    void EstadoAlarmaActiva(EventosJoystick evento);
    void MenuPrincipal(EventosJoystick evento);

  //Joystick
    EventosJoystick leerJoystick();

  //Alarmas

  //Pantalla
    void mostrarHora();
    void mostrarMenuPrincipal();
    void mostrarErrorRTC();

  //Servomotor
    void iniciarCampana();
    void actualizarCampana();
    void detenerCampana();

  //Leds
    void escribirLeds(uint8_t patron);
    void encenderGaveta(uint8_t gaveta);
    //void apagarTodasLasGavetas();

  //Memoria EEPROM
    //void cargarAlarmas();
    void guardarAlarmas();

// ============================================================
// SETUP
// ============================================================
  void setup() {
    Serial.begin(115200); 

    inicializarPines();
    inicializarPantalla();
    inicializarServo();
    //cargarAlarmas();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Iniciando..."));

    rtcDisponible = inicializarRTC();

    if(rtcDisponible){

      actualizarReloj();
      cambiarEstado(EstadoSistema::NORMAL);

    }else {

      cambiarEstado(EstadoSistema::ERROR_RTC);

    }


  }


// ============================================================
// LOOP PRINCIPAL
// ============================================================
  void loop() {

    actualizarReloj();

    //EventoJoystick
    EventosJoystick evento = leerJoystick();

    imprimirEventosJoystick(evento);

    actualizarEstado(evento);

  }



// ******************* FNCIONES DEL SISTEMA *********************
  // ============================================================
  // INICIALIZACIÓN
  // ============================================================
    void inicializarPines(){
      pinMode(PIN_X, INPUT);
      pinMode(PIN_Y, INPUT);
      pinMode(BOTTON, INPUT_PULLUP);

      //apagarTodasLasGavetas();

    }
    void inicializarPantalla(){
      lcd.init();
      lcd.backlight();
      lcd.clear();

    }
    bool inicializarRTC(){
      if(!rtc.begin()){
        Serial.println(F("ERROR: No se encontro el RTC."));
        return false;
      }
      if(rtc.lostPower()){
        Serial.println(F("ADVERTENCIA: Hora perdida."));
        Serial.println(F("Ajustando"));

        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      }

      fechaHoraActual = rtc.now();
      ultimaLecturaRTC = millis();

      Serial.println(F("RTC detectado correctamente."));
      return true;
        
    }
    void inicializarServo(){

      if(servoCampana.attached()){
        servoCampana.detach();
      }

    }


  // ============================================================
  // CAMBIO Y CONTROL DE ESTADOS
  // ============================================================
  void cambiarEstado(EstadoSistema nuevoEstado){
    estadoActual = nuevoEstado;
    momentoEntradaEstado = millis();
    pantallaDebeActualizarse = true;

    lcd.clear();

    switch (estadoActual) {
      case EstadoSistema::NORMAL:
        indiceAlarmaActiva = -1;
        mostrarHora();
        break;
      
      case EstadoSistema::ALARMA_ACTIVA:


        break;

      case EstadoSistema::MENU_PRINCIPAL:

        mostrarMenuPrincipal();
        break;

      case EstadoSistema::ERROR_RTC:

        mostrarErrorRTC();
        break;
        
      case EstadoSistema::INICIANDO:

        break;   

    }
  }

  void actualizarEstado(EventosJoystick evento){
    switch (estadoActual) {
      case EstadoSistema::NORMAL:
        EstadoNormal(evento); 
        break;
      
      case EstadoSistema::ALARMA_ACTIVA:

        //AlarmaActiva(evento);
        break;

      case EstadoSistema::MENU_PRINCIPAL:

        MenuPrincipal(evento);
        break;

      case EstadoSistema::ERROR_RTC:

        ErrorRTC(evento);
        break;
        
      case EstadoSistema::INICIANDO:

        break;   

    }

  }


  // ============================================================
  // LECTURA DEL RELOJ
  // ============================================================
  void actualizarReloj(){
    if(!rtcDisponible){
      return;
    }
    unsigned long ahoraMs = millis();

    if(ahoraMs - ultimaLecturaRTC >= INTERVALO_LECTURA_RTC_MS){
      ultimaLecturaRTC = ahoraMs;
      fechaHoraActual = rtc.now();
    }
  }


  // ============================================================
  // ESTADO NORMAL
  // ============================================================
  void EstadoNormal(EventosJoystick evento){
    mostrarHora();

    //Boton abre el menu, si no hay alarma activa
    if(evento == EventosJoystick::BOTON){
      cambiarEstado(EstadoSistema::MENU_PRINCIPAL);
    }

  }


  // ============================================================
  // ESTADO DE ALARMA
  // ============================================================



  // ============================================================
  // ESTADO MOSTRAR DOSIS
  // ============================================================



  // ============================================================
  // MENÚ PRINCIPAL
  // ============================================================
  
  void MenuPrincipal(EventosJoystick evento){
    /*
      TODO:
      - Arriba y abajo moverán la selección.
      - Botón confirmará.
      - Izquierda regresará al modo normal.
    */
    if(evento == EventosJoystick::ARRIBA) {
      opcionMenuPrincipal = 1;
    }else if(evento == EventosJoystick::ABAJO){
      opcionMenuPrincipal = 0;
    }else{
      opcionMenuPrincipal = opcionMenuPrincipal;
    }
    mostrarMenuPrincipal();

    if (evento == EventosJoystick::IZQUIERDA){
      cambiarEstado(EstadoSistema::NORMAL);
    }
  }


  // ============================================================
  // CREAR ALARMA
  // ============================================================



  // ============================================================
  // ELIMINAR ALARMA
  // ============================================================



  // ============================================================
  // AJUSTAR HORA
  // ============================================================



  // ============================================================
  // ERROR DEL RTC
  // ============================================================
    void ErrorRTC(EventosJoystick evento){
      /*
        Al presionar el botón se intenta detectar nuevamente
        el módulo RTC.
      */
      if(evento == EventosJoystick::BOTON){
        rtcDisponible = inicializarRTC();

        if(rtcDisponible){
          actualizarReloj();
          cambiarEstado(EstadoSistema::NORMAL);
        }
      }
    }


  // ============================================================
  // JOYSTICK
  // ============================================================
    EventosJoystick leerJoystick(){
      // ----------------------------------------------------------
      // Variables persistentes del botón
      // ----------------------------------------------------------

      static bool lecturaAnteriorBoton = HIGH;
      static bool estadoEstableBoton = HIGH;
      static unsigned long ultimoCambioBoton = 0;
 

      // Evita repetir continuamente una dirección mientras
      // el joystick se mantiene inclinado. 
      static bool joystickPreparado = true;

      // ----------------------------------------------------------
      // Lectura del botón
      // ----------------------------------------------------------

      bool lecturaBoton =
        digitalRead(BOTTON);

      // Detectar un cambio físico en la lectura
      if (lecturaBoton != lecturaAnteriorBoton) {
        lecturaAnteriorBoton = lecturaBoton;
        ultimoCambioBoton = millis();
      }

      // Confirmar que el cambio se mantuvo durante el tiempo
      // de antirrebote
      if (
        millis() - ultimoCambioBoton >= ANTIRREBOTE_BOTON_MS &&
        lecturaBoton != estadoEstableBoton
      ) {
        estadoEstableBoton = lecturaBoton;

        // Con INPUT_PULLUP, LOW significa botón presionado
        if (estadoEstableBoton == LOW) {
          return EventosJoystick::BOTON;
        }
      }

      // ----------------------------------------------------------
      // Lectura de los ejes analógicos
      // ----------------------------------------------------------

      int valorX = analogRead(PIN_X);
      int valorY = analogRead(PIN_Y);

      bool ejeXCentrado =
        valorX >= JOYSTICK_CENTRO_BAJO &&
        valorX <= JOYSTICK_CENTRO_ALTO;

      bool ejeYCentrado =
        valorY >= JOYSTICK_CENTRO_BAJO &&
        valorY <= JOYSTICK_CENTRO_ALTO;

      // Cuando regresa al centro se permite generar otro evento
      if (ejeXCentrado && ejeYCentrado) {
        joystickPreparado = true;
        return EventosJoystick::NINGUNO;
      }

      // Si todavía no ha regresado al centro, no repetir evento
      if (!joystickPreparado) {
        return EventosJoystick::NINGUNO;
      }

      EventosJoystick evento = EventosJoystick::NINGUNO;

      // Determinar cuál eje tiene el movimiento mayor
      int distanciaX = abs(valorX - 512);
      int distanciaY = abs(valorY - 512);

      if (distanciaX >= distanciaY) {
        // Movimiento horizontal

        if (valorX <= JOYSTICK_LIMITE_BAJO) {
          evento = EventosJoystick::IZQUIERDA;
        } else if (valorX >= JOYSTICK_LIMITE_ALTO) {
          evento = EventosJoystick::DERECHA;
        }
      } else {
        // Movimiento vertical

        if (valorY <= JOYSTICK_LIMITE_BAJO) {
          evento = EventosJoystick::ARRIBA;
        } else if (valorY >= JOYSTICK_LIMITE_ALTO) {
          evento = EventosJoystick::ABAJO;
        }
      }

      if (evento != EventosJoystick::NINGUNO) {
        joystickPreparado = false;
      }


      return evento;

    }


  // ============================================================
  // IMPRIMIR EVENTOS
  // ============================================================
    void imprimirEventosJoystick(EventosJoystick evento) {
      switch (evento) {
        case EventosJoystick::ARRIBA:
          Serial.print(F("Joystick: ARRIBA  "));
          Serial.println(opcionMenuPrincipal);
          break;

        case EventosJoystick::ABAJO:
          Serial.print(F("Joystick: ABAJO "));
          Serial.println(opcionMenuPrincipal);
          break;

        case EventosJoystick::IZQUIERDA:
          Serial.println(F("Joystick: IZQUIERDA"));
          break;

        case EventosJoystick::DERECHA:
          Serial.println(F("Joystick: DERECHA"));
          break;

        case EventosJoystick::BOTON:
          Serial.println(F("Joystick: BOTON"));
          break;

        case EventosJoystick::NINGUNO:
          break;
      }
    }

  // ============================================================
  // COMPROBACIÓN DE ALARMAS
  // ============================================================


    /*
      TODO:
      Recorrer el arreglo de alarmas y comprobar:

      - Que la alarma esté activa.
      - Que coincida la hora.
      - Que coincida el minuto.
      - Que corresponda al día actual.
      - Que no haya sonado ya durante ese minuto.
    */



  // ============================================================
  // PANTALLA
  // ============================================================
  void mostrarHora(){
    if(!rtcDisponible){
      return;
    }

    static uint8_t ultimoSegundoMostrado = 255;
    uint8_t segundoActual = fechaHoraActual.second();

    if (!pantallaDebeActualizarse && segundoActual == ultimoSegundoMostrado){
      return;
    }

    ultimoSegundoMostrado = segundoActual;
    pantallaDebeActualizarse = false;

    char lineaHora[17];
    char lineaFecha[17];

    snprintf(
      lineaHora,
      sizeof(lineaHora),
      "Hora: %02u:%02u:%02u  ",
      static_cast<unsigned int>(fechaHoraActual.hour()),
      static_cast<unsigned int>(fechaHoraActual.minute()),
      static_cast<unsigned int>(fechaHoraActual.second())
    );

    snprintf(
      lineaFecha,
      sizeof(lineaFecha),
      "Fecha:%02u/%02u/%04u      ",
      static_cast<unsigned int>(fechaHoraActual.day()),
      static_cast<unsigned int>(fechaHoraActual.month()),
      static_cast<unsigned int>(fechaHoraActual.year())
    );

    lcd.setCursor(0,0); 
    lcd.print(lineaHora);

    lcd.setCursor(0,1); 
    lcd.print(lineaFecha);
  }

  
  void mostrarMenuPrincipal(){

    lcd.setCursor(0,0);
    lcd.print(opcionMenuPrincipal == 1 
      ? F(">Crear alarma  ")
      : F(" Crear alarma  "));

    lcd.setCursor(0,1);
    lcd.print(opcionMenuPrincipal == 0 
      ? F(">Eliminar      ")
      : F(" Eliminar      "));
  }

  void mostrarErrorRTC(){
    lcd.setCursor(0,0);
    lcd.print(F("ERROR DEL RELOJ"));

    lcd.setCursor(0,1);
    lcd.print(F("Boton: reint."));
    
  }


  // ============================================================
  // CONTROL DEL SERVOMOTOR
  // ============================================================
  void iniciarCampana(){
    if(!servoCampana.attached()){
      servoCampana.attach(PIN_SERVO);
    }

    servoEnPosicionDerecha = false;
    servoCampana.write(60);

    ultimoMovimientoServo= millis();
  }

  void actualizarCampana() {
    unsigned long ahoraMs = millis();

    if (ahoraMs - ultimoMovimientoServo < INTERVALO_SERVO_MS) {
      return;
    }

    ultimoMovimientoServo = ahoraMs;
    servoEnPosicionDerecha = !servoEnPosicionDerecha;

    if (servoEnPosicionDerecha) {
      servoCampana.write(120);
    } else {
      servoCampana.write(60);
    }
  }

  void detenerCampana() {
    if (!servoCampana.attached()) {
      return;
    }

    servoCampana.write(90);

    /*
      Más adelante evaluaremos si conviene esperar brevemente
      antes de usar detach().
    */

    servoCampana.detach();
  }
  // ============================================================
  // CONTROL DE LOS LEDS
  // ============================================================



  // ============================================================
  // MEMORIA EEPROM
  // ============================================================

