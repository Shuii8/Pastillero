#include <Wire.h>               // Librería para comunicación I2C
#include <LiquidCrystal_I2C.h>  // Librería para el LCD con adaptador I2C
#include <RTClib.h>             // Librería para el módulo RTC    
#include <Servo.h>              // Librería para el control del servomotor
#include <EEPROM.h>             // Librería para el acceso a la memoria EEPROM
#include <avr/pgmspace.h>
#include <Arduino.h>


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
  constexpr uint8_t LONGITUD_NOMBRE = 14;
  constexpr uint8_t MAX_ALARMAS = 8;
  constexpr uint8_t NUM_GAVETAS = 8;
  uint8_t alarmasPendientes = 0;
  uint32_t minutoRTCProcesado = 0xFFFFFFFFUL;
  constexpr uint8_t ANGULO_CENTRO_SERVO = 90;
  constexpr unsigned long TIEMPO_CENTRADO_SERVO_MS = 500;

  int8_t indiceAlarmaEliminar = -1;
  uint8_t opcionConfirmacionEliminar = 1;

  uint8_t diaRelojEdicion = 1;
  uint8_t mesRelojEdicion = 1;
  uint16_t anioRelojEdicion = 2026;

  uint8_t hora12RelojEdicion = 12;
  uint8_t minutoRelojEdicion = 0;
  bool periodoPMRelojEdicion = false;

  // 0 = guardar
  // 1 = salir sin guardar
  uint8_t opcionConfirmacionReloj = 0;

  bool relojGuardadoEsperandoSalida = false;

  constexpr unsigned long RETARDO_RECONOCER_ALARMA_MS = 500UL; 
  constexpr unsigned long INTERVALO_LECTURA_RTC_MS = 250UL;
  constexpr unsigned long INTERVALO_SERVO_MS = 400UL;
  constexpr unsigned long DURACION_MOSTRAR_DOSIS = 120000UL;

  const char MEDICINAS[][LONGITUD_NOMBRE + 1] PROGMEM = {
    "Nueva",
    "Aspirina",
    "Metformina",
    "Losartan",
    "Paracetamol",
    "Vitamina"
  };

  constexpr uint8_t NUM_MEDICINAS =
    sizeof(MEDICINAS) / sizeof(MEDICINAS[0]);

  const char DOSIS[][17] PROGMEM = {
    "1/4 tableta",
    "1/2 tableta",
    "1 tableta",
    "1 1/2 tabletas",
    "2 tabletas",
    "1 capsula",
    "2 capsulas",
    "5 mL"
  };
  constexpr uint8_t NUM_DOSIS =
    sizeof(DOSIS) / sizeof(DOSIS[0]);

  const char NOMBRES_DIAS[][10] PROGMEM = {
    "Domingo",
    "Lunes",
    "Martes",
    "Miercoles",
    "Jueves",
    "Viernes",
    "Sabado"
  };

  struct Alarma {
    uint8_t activa;
    uint8_t dias;
    uint8_t hora;
    uint8_t minuto;
    uint8_t dosisId;
    uint8_t gaveta;
    char nombre[LONGITUD_NOMBRE + 1];
  };
  Alarma alarmaEnEdicion;
  Alarma alarmas[MAX_ALARMAS];

  // EEPROM:
  // bytes 0-1: firma
  // desde byte 2: arreglo de alarmas
  constexpr uint16_t FIRMA_EEPROM = 0x5042;
  constexpr int DIRECCION_FIRMA = 0;
  constexpr int DIRECCION_ALARMAS = sizeof(FIRMA_EEPROM);

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
    constexpr uint8_t PIN_595_DATOS = 4;
    constexpr uint8_t PIN_595_RELOJ = 5;
    constexpr uint8_t PIN_595_LATCH = 6;

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
    AJUSTAR_RELOJ,
    CREAR_ALARMA,
    ALARMA_ACTIVA,
    MOSTRAR_DOSIS,
    ELIMINAR_ALARMA,
    ERROR_RTC

  };
  EstadoSistema estadoActual = EstadoSistema::INICIANDO;

// ============================================================
// SUB-ESTADOS DEL SISTEMA
// ============================================================
  enum class PasoCrearAlarma : uint8_t {
    SELECCIONAR_DIAS,
    SELECCIONAR_HORA,
    SELECCIONAR_MEDICINA,
    EDITAR_NOMBRE,
    SELECCIONAR_LETRA,
    SELECCIONAR_DOSIS,
    SELECCIONAR_GAVETA,
    CONFIRMAR
  };
  PasoCrearAlarma pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_DIAS;

  enum class PasoEliminarAlarma : uint8_t {
    SELECCIONAR,
    CONFIRMAR,
    RESULTADO
  };

  PasoEliminarAlarma pasoEliminarAlarma =
    PasoEliminarAlarma::SELECCIONAR;


  enum class CampoAjusteReloj : uint8_t {
    DIA,
    MES,
    ANIO,
    HORA,
    MINUTO,
    PERIODO,
    CONFIRMAR
  };

  CampoAjusteReloj campoAjusteReloj =
    CampoAjusteReloj::DIA;

  //Variables temporales:
    uint8_t opcionDia = 0;
    uint8_t campoHora = 0;
    uint8_t hora12Edicion = 8;
    uint8_t minutoEdicion = 0;
    bool periodoPM = false;

    uint8_t opcionMedicina = 0;
    uint8_t opcionDosis = 0;
    uint8_t gavetaEdicion = 0;

    uint8_t posicionNombre = 0;
    uint8_t filaLetra = 0;
    uint8_t columnaLetra = 0;

    bool focoAccionesNombre = false;
    uint8_t accionNombre = 0;  // 0 = salir, 1 = guardar

    uint8_t opcionConfirmacion = 0;  // 0 guardar, 1 volver
    bool alarmaGuardadaEsperandoSalida = false;

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
// VARIABLES GLOBALES DEL SISTEMA
// ============================================================
  
  DateTime fechaHoraActual; 

  uint8_t convertirHora24(uint8_t hora12, bool esPM) {
    uint8_t hora24 = hora12 % 12;

    if (esPM) {
      hora24 += 12;
    }

    return hora24;
  }

  bool rtcDisponible = false;
  bool pantallaDebeActualizarse = true;
  
  int8_t indiceAlarmaActiva = -1;
  uint8_t opcionMenuPrincipal = 0;

  unsigned long momentoEntradaEstado = 0;
  unsigned long ultimaLecturaRTC = 0;
  unsigned long ultimoMovimientoServo = 0;
  bool servoEnPosicionDerecha = false;

  constexpr uint8_t COLUMNAS_FILA_LETRAS[2] = {
    15,  // A-M, borrar y espacio
    13   // N-Z
  };


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

  //Estados
    void EstadoNormal(EventosJoystick evento);
    void EstadoAlarmaActiva(EventosJoystick evento);
    void MenuPrincipal(EventosJoystick evento);
    void EstadoCrearAlarma(EventosJoystick evento);
    void EstadoMostrarDosis(EventosJoystick evento);
    void ErrorRTC(EventosJoystick evento);

  //Joystick
    EventosJoystick leerJoystick();
    void imprimirEventosJoystick(EventosJoystick evento);

  //Crear alarmas
    void iniciarCreacionAlarma();
    void procesarSeleccionDias(EventosJoystick evento);
    void procesarSeleccionHora(EventosJoystick evento);
    void procesarSeleccionMedicina(EventosJoystick evento);
    void procesarEditorNombre(EventosJoystick evento);
    void procesarSelectorLetra(EventosJoystick evento);
    void procesarSeleccionDosis(EventosJoystick evento);
    void procesarSeleccionGaveta(EventosJoystick evento);
    void procesarConfirmacionAlarma(EventosJoystick evento);

    bool alarmaCoincideAhora(const Alarma &alarma);
    void actualizarAlarmas();
    void activarSiguienteAlarmaPendiente();

    void mostrarAlarmaActiva();
    void mostrarDosisActiva();

    void borrarTodasLasAlarmas();

  //Pantalla
    void mostrarHora();
    void mostrarMenuPrincipal();
    void mostrarErrorRTC();
    void mostrarSeleccionDias();
    void mostrarSeleccionHora();
    void mostrarSeleccionMedicina();
    void mostrarEditorNombre();
    void mostrarSelectorLetra();
    void mostrarSeleccionDosis();
    void mostrarSeleccionGaveta();
    void mostrarConfirmacionAlarma();
    void mostrarMensaje(const __FlashStringHelper *linea1,
                        const __FlashStringHelper *linea2);

  // Utilidades
    uint8_t convertirHora24(uint8_t hora12, bool esPM);
    void copiarNombrePredefinido(uint8_t indice, char *destino);
    void copiarDosis(uint8_t indice, char *destino);
    bool nombreTieneLetras();
    char obtenerLetraSeleccionada();
    int8_t buscarEspacioLibre();
    void imprimirAlarmas();
    void imprimirDiasAlarma(uint8_t dias);
    bool alarmaAlmacenadaValida(const Alarma &alarma);
    

  //Servomotor
    void iniciarCampana();
    void actualizarCampana();
    void detenerCampana();

  //Leds
    void escribirLeds(uint8_t patron);
    void encenderGaveta(uint8_t gaveta);
    void apagarTodasLasGavetas();

  //Memoria EEPROM
    void cargarAlarmas();
    void guardarAlarmas();

  // Estados que faltaban declarar
    void EstadoCrearAlarma(EventosJoystick evento);

  // Eliminar alarmas
    void iniciarEliminacionAlarma();
    void EstadoEliminarAlarma(EventosJoystick evento);
    void mostrarSeleccionEliminar();
    void mostrarConfirmacionEliminar();
    void mostrarResultadoEliminar();

    int8_t buscarSiguienteAlarmaActiva(
      int8_t indiceActual,
      int8_t direccion
    );

    void eliminarAlarma(uint8_t indice);
    void copiarNombreVisible(
      const Alarma &alarma,
      char *destino
    );

  // Ajustar reloj
    void iniciarAjusteReloj();
    void EstadoAjustarReloj(EventosJoystick evento);
    void mostrarAjusteReloj();
    void modificarCampoReloj(int8_t cambio);
    void guardarAjusteReloj();

    bool esAnioBisiesto(uint16_t anio);
    uint8_t obtenerDiasDelMes(
      uint8_t mes,
      uint16_t anio
    );

    void limitarDiaReloj();

// ============================================================
// SETUP
// ============================================================
  void setup() {
    Serial.begin(115200); 

    inicializarPines();
    inicializarPantalla();
    inicializarServo();
    cargarAlarmas();
    imprimirAlarmas();

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
    actualizarAlarmas();

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
 
      pinMode(PIN_595_DATOS, OUTPUT);
      pinMode(PIN_595_RELOJ, OUTPUT);
      pinMode(PIN_595_LATCH, OUTPUT);

      digitalWrite(PIN_595_DATOS, LOW);
      digitalWrite(PIN_595_RELOJ, LOW);
      digitalWrite(PIN_595_LATCH, LOW);

      apagarTodasLasGavetas();

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
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      }
      //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
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

      lcd.noBlink();
      lcd.clear();

      switch (estadoActual) {
        case EstadoSistema::NORMAL:

          detenerCampana();
          apagarTodasLasGavetas();

          indiceAlarmaActiva = -1;
          mostrarHora();
          break;

        case EstadoSistema::MENU_PRINCIPAL:

          mostrarMenuPrincipal();
          break;

        case EstadoSistema::CREAR_ALARMA:

          mostrarSeleccionDias();
          break;

        case EstadoSistema::ERROR_RTC:
          detenerCampana();
          apagarTodasLasGavetas();
          mostrarErrorRTC();
          break;
          
        case EstadoSistema::INICIANDO:

          break;  

        case EstadoSistema::ALARMA_ACTIVA:

           if (
                indiceAlarmaActiva >= 0 &&
                indiceAlarmaActiva < MAX_ALARMAS
              ) {
                iniciarCampana();

                encenderGaveta(
                  alarmas[indiceAlarmaActiva].gaveta
                );

                mostrarAlarmaActiva();
            }
          break;

        case EstadoSistema::MOSTRAR_DOSIS:

          detenerCampana();
          apagarTodasLasGavetas();
          mostrarDosisActiva();       
          break; 

        case EstadoSistema::ELIMINAR_ALARMA:
          mostrarSeleccionEliminar();
          break;

        case EstadoSistema::AJUSTAR_RELOJ:
          mostrarAjusteReloj();
          break;

      }
    }

    void actualizarEstado(EventosJoystick evento){
      switch (estadoActual) {
        case EstadoSistema::NORMAL:
          EstadoNormal(evento); 
          break;

        case EstadoSistema::MENU_PRINCIPAL:

          MenuPrincipal(evento);
          break;

        case EstadoSistema::CREAR_ALARMA:
          EstadoCrearAlarma(evento);
          break;

        case EstadoSistema::ERROR_RTC:

          ErrorRTC(evento);
          break;
          
        case EstadoSistema::INICIANDO:

          break;   
        
        case EstadoSistema::ALARMA_ACTIVA:

          EstadoAlarmaActiva(evento);
          break;

        case EstadoSistema::MOSTRAR_DOSIS:

          EstadoMostrarDosis(evento);
          break;

        case EstadoSistema::ELIMINAR_ALARMA:
          EstadoEliminarAlarma(evento);
          break;

        case EstadoSistema::AJUSTAR_RELOJ:
          EstadoAjustarReloj(evento);
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

        void iniciarAjusteReloj() {
      DateTime ahora = rtc.now();

      diaRelojEdicion = ahora.day();
      mesRelojEdicion = ahora.month();
      anioRelojEdicion = ahora.year();

      uint8_t hora24 = ahora.hour();

      hora12RelojEdicion = hora24 % 12;

      if (hora12RelojEdicion == 0) {
        hora12RelojEdicion = 12;
      }

      minutoRelojEdicion = ahora.minute();
      periodoPMRelojEdicion = hora24 >= 12;

      campoAjusteReloj =
        CampoAjusteReloj::DIA;

      opcionConfirmacionReloj = 0;
      relojGuardadoEsperandoSalida = false;

      cambiarEstado(
        EstadoSistema::AJUSTAR_RELOJ
      );
    }
    bool esAnioBisiesto(uint16_t anio) {
      if (anio % 400 == 0) {
        return true;
      }

      if (anio % 100 == 0) {
        return false;
      }

      return anio % 4 == 0;
    }
    uint8_t obtenerDiasDelMes(
        uint8_t mes,
        uint16_t anio
      ) {
      static const uint8_t DIAS_POR_MES[12] = {
        31, 28, 31, 30,
        31, 30, 31, 31,
        30, 31, 30, 31
      };

      if (mes < 1 || mes > 12) {
        return 31;
      }

      if (
        mes == 2 &&
        esAnioBisiesto(anio)
      ) {
        return 29;
      }

      return DIAS_POR_MES[mes - 1];
    }
    void limitarDiaReloj() {
      uint8_t maximoDia =
        obtenerDiasDelMes(
          mesRelojEdicion,
          anioRelojEdicion
        );

      if (diaRelojEdicion > maximoDia) {
        diaRelojEdicion = maximoDia;
      }

      if (diaRelojEdicion < 1) {
        diaRelojEdicion = 1;
      }
    }

    void modificarCampoReloj(int8_t cambio) {
      switch (campoAjusteReloj) {

        case CampoAjusteReloj::DIA: {
          uint8_t maximo =
            obtenerDiasDelMes(
              mesRelojEdicion,
              anioRelojEdicion
            );

          if (cambio > 0) {
            diaRelojEdicion =
              diaRelojEdicion >= maximo
                ? 1
                : diaRelojEdicion + 1;
          } else {
            diaRelojEdicion =
              diaRelojEdicion <= 1
                ? maximo
                : diaRelojEdicion - 1;
          }

          break;
        }

        case CampoAjusteReloj::MES:
          if (cambio > 0) {
            mesRelojEdicion =
              mesRelojEdicion >= 12
                ? 1
                : mesRelojEdicion + 1;
          } else {
            mesRelojEdicion =
              mesRelojEdicion <= 1
                ? 12
                : mesRelojEdicion - 1;
          }

          limitarDiaReloj();
          break;

        case CampoAjusteReloj::ANIO:
          if (cambio > 0) {
            anioRelojEdicion =
              anioRelojEdicion >= 2099
                ? 2000
                : anioRelojEdicion + 1;
          } else {
            anioRelojEdicion =
              anioRelojEdicion <= 2000
                ? 2099
                : anioRelojEdicion - 1;
          }

          limitarDiaReloj();
          break;

        case CampoAjusteReloj::HORA:
          if (cambio > 0) {
            hora12RelojEdicion =
              hora12RelojEdicion >= 12
                ? 1
                : hora12RelojEdicion + 1;
          } else {
            hora12RelojEdicion =
              hora12RelojEdicion <= 1
                ? 12
                : hora12RelojEdicion - 1;
          }

          break;

        case CampoAjusteReloj::MINUTO:
          if (cambio > 0) {
            minutoRelojEdicion =
              minutoRelojEdicion >= 59
                ? 0
                : minutoRelojEdicion + 1;
          } else {
            minutoRelojEdicion =
              minutoRelojEdicion == 0
                ? 59
                : minutoRelojEdicion - 1;
          }

          break;

        case CampoAjusteReloj::PERIODO:
          periodoPMRelojEdicion =
            !periodoPMRelojEdicion;
          break;

        case CampoAjusteReloj::CONFIRMAR:
          break;
      }
    }
    void guardarAjusteReloj() {
      uint8_t hora24 =
        convertirHora24(
          hora12RelojEdicion,
          periodoPMRelojEdicion
        );

      DateTime nuevaFechaHora(
        anioRelojEdicion,
        mesRelojEdicion,
        diaRelojEdicion,
        hora24,
        minutoRelojEdicion,
        0
      );

      rtc.adjust(nuevaFechaHora);

      fechaHoraActual = rtc.now();
      ultimaLecturaRTC = millis();
      pantallaDebeActualizarse = true;

      Serial.println(F("Reloj actualizado:"));

      Serial.print(diaRelojEdicion);
      Serial.print('/');
      Serial.print(mesRelojEdicion);
      Serial.print('/');
      Serial.print(anioRelojEdicion);
      Serial.print(' ');

      if (hora24 < 10) {
        Serial.print('0');
      }

      Serial.print(hora24);
      Serial.print(':');

      if (minutoRelojEdicion < 10) {
        Serial.print('0');
      }

      Serial.println(minutoRelojEdicion);

      relojGuardadoEsperandoSalida = true;

      mostrarMensaje(
        F("Reloj ajustado"),
        F("Boton: volver")
      );
    }

  // ============================================================
  // ESTADO MOSTRAR DOSIS
  // ============================================================
    void EstadoAjustarReloj(
        EventosJoystick evento
      ) {
      // Después de guardar, cualquier acción regresa
      // a la pantalla principal del reloj.
      if (relojGuardadoEsperandoSalida) {
        if (evento != EventosJoystick::NINGUNO) {
          relojGuardadoEsperandoSalida = false;

          cambiarEstado(
            EstadoSistema::NORMAL
          );
        }

        return;
      }

      uint8_t campo =
        static_cast<uint8_t>(
          campoAjusteReloj
        );

      uint8_t campoConfirmar =
        static_cast<uint8_t>(
          CampoAjusteReloj::CONFIRMAR
        );

      // ----------------------------------------------------------
      // PANTALLA DE CONFIRMACIÓN
      // ----------------------------------------------------------
      if (campo == campoConfirmar) {
        if (
          evento == EventosJoystick::IZQUIERDA ||
          evento == EventosJoystick::DERECHA ||
          evento == EventosJoystick::ARRIBA ||
          evento == EventosJoystick::ABAJO
        ) {
          opcionConfirmacionReloj =
            opcionConfirmacionReloj == 0
              ? 1
              : 0;

          mostrarAjusteReloj();
          return;
        }

        if (evento == EventosJoystick::BOTON) {
          if (opcionConfirmacionReloj == 0) {
            guardarAjusteReloj();
          } else {
            cambiarEstado(
              EstadoSistema::MENU_PRINCIPAL
            );
          }
        }

        return;
      }

      // ----------------------------------------------------------
      // REGRESAR AL CAMPO ANTERIOR
      // ----------------------------------------------------------
      if (evento == EventosJoystick::IZQUIERDA) {
        if (
          campoAjusteReloj ==
          CampoAjusteReloj::DIA
        ) {
          opcionMenuPrincipal = 2;

          cambiarEstado(
            EstadoSistema::MENU_PRINCIPAL
          );

          return;
        }

        campoAjusteReloj =
          static_cast<CampoAjusteReloj>(
            campo - 1
          );

        mostrarAjusteReloj();
        return;
      }

      // ----------------------------------------------------------
      // AVANZAR AL CAMPO SIGUIENTE
      // ----------------------------------------------------------
      if (
        evento == EventosJoystick::DERECHA ||
        evento == EventosJoystick::BOTON
      ) {
        campoAjusteReloj =
          static_cast<CampoAjusteReloj>(
            campo + 1
          );

        mostrarAjusteReloj();
        return;
      }

      // ----------------------------------------------------------
      // MODIFICAR EL VALOR
      // ----------------------------------------------------------
      if (evento == EventosJoystick::ARRIBA) {
        modificarCampoReloj(1);
        mostrarAjusteReloj();
        return;
      }

      if (evento == EventosJoystick::ABAJO) {
        modificarCampoReloj(-1);
        mostrarAjusteReloj();
      }
    }

  // ============================================================
  // ESTADO NORMAL
  // ============================================================
    void EstadoNormal(EventosJoystick evento){
      mostrarHora();

      //Boton abre el menu, si no hay alarma activa
      if(evento == EventosJoystick::BOTON){
        opcionMenuPrincipal = 0;
        cambiarEstado(EstadoSistema::MENU_PRINCIPAL);
      }

    }
  // ============================================================
  // ESTADO DE ALARMA
  // ============================================================
    void EstadoAlarmaActiva(EventosJoystick evento) {
      actualizarCampana();

      bool puedeReconocerse =
        millis() - momentoEntradaEstado >=
        RETARDO_RECONOCER_ALARMA_MS;

      if (
        puedeReconocerse &&
        evento != EventosJoystick::NINGUNO
      ) {
        cambiarEstado(EstadoSistema::MOSTRAR_DOSIS);
      }
    }

  // ============================================================
  // ESTADO MOSTRAR DOSIS
  // ============================================================
    void EstadoMostrarDosis(EventosJoystick evento) {
      bool tiempoCumplido =
        millis() - momentoEntradaEstado >=
        DURACION_MOSTRAR_DOSIS;

      bool botonPresionado =
        evento == EventosJoystick::BOTON;

      if (!tiempoCumplido && !botonPresionado) {
        return;
      }

      cambiarEstado(EstadoSistema::NORMAL);

      // Si había otra alarma a la misma hora, se activa ahora.
      if (alarmasPendientes != 0) {
        activarSiguienteAlarmaPendiente();
      }
    }

  // ============================================================
  // MENÚ PRINCIPAL
  // ============================================================
  
    void MenuPrincipal(EventosJoystick evento) {
      bool cambio = false;

      if (evento == EventosJoystick::ARRIBA) {
        opcionMenuPrincipal =
          opcionMenuPrincipal == 0
            ? 2
            : opcionMenuPrincipal - 1;

        cambio = true;

      } else if (evento == EventosJoystick::ABAJO) {
        opcionMenuPrincipal =
          opcionMenuPrincipal >= 2
            ? 0
            : opcionMenuPrincipal + 1;

        cambio = true;

      } else if (evento == EventosJoystick::IZQUIERDA) {
        cambiarEstado(EstadoSistema::NORMAL);
        return;

      } else if (evento == EventosJoystick::BOTON) {
        switch (opcionMenuPrincipal) {
          case 0:
            iniciarCreacionAlarma();
            break;

          case 1:
            iniciarEliminacionAlarma();
            break;

          case 2:
            iniciarAjusteReloj();
            break;
        }

        return;
      }

      if (cambio) {
        mostrarMenuPrincipal();
      }
    }


  // ============================================================
  // CREAR ALARMA
  // ============================================================
    void iniciarCreacionAlarma() {
      memset(&alarmaEnEdicion, 0, sizeof(alarmaEnEdicion));

      alarmaEnEdicion.activa = 1;
      alarmaEnEdicion.hora = 8;
      alarmaEnEdicion.minuto = 0;
      alarmaEnEdicion.gaveta = 0;

      memset(
        alarmaEnEdicion.nombre,
        ' ',
        LONGITUD_NOMBRE
      );

      alarmaEnEdicion.nombre[LONGITUD_NOMBRE] = '\0';

      opcionDia = 0;
      campoHora = 0;
      hora12Edicion = 8;
      minutoEdicion = 0;
      periodoPM = false;

      opcionMedicina = 0;
      opcionDosis = 0;
      gavetaEdicion = 0;

      posicionNombre = 0;
      filaLetra = 0;
      columnaLetra = 0;

      focoAccionesNombre = false;
      accionNombre = 0;
      opcionConfirmacion = 0;
      alarmaGuardadaEsperandoSalida = false;

      pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_DIAS;
      cambiarEstado(EstadoSistema::CREAR_ALARMA);
    }
    void EstadoCrearAlarma(EventosJoystick evento) {
      switch (pasoCrearAlarma) {
        case PasoCrearAlarma::SELECCIONAR_DIAS:
          procesarSeleccionDias(evento);
          break;

        case PasoCrearAlarma::SELECCIONAR_HORA:
          procesarSeleccionHora(evento);
          break;

        case PasoCrearAlarma::SELECCIONAR_MEDICINA:
          procesarSeleccionMedicina(evento);
          break;

        case PasoCrearAlarma::EDITAR_NOMBRE:
          procesarEditorNombre(evento);
          break;

        case PasoCrearAlarma::SELECCIONAR_LETRA:
          procesarSelectorLetra(evento);
          break;

        case PasoCrearAlarma::SELECCIONAR_DOSIS:
          procesarSeleccionDosis(evento);
          break;

        case PasoCrearAlarma::SELECCIONAR_GAVETA:
          procesarSeleccionGaveta(evento);
          break;

        case PasoCrearAlarma::CONFIRMAR:
          procesarConfirmacionAlarma(evento);
          break;
      }
    }

  // ============================================================
  // ELIMINAR ALARMA
  // ============================================================

    int8_t buscarSiguienteAlarmaActiva(
        int8_t indiceActual,
        int8_t direccion
      ) {
      if (direccion == 0) {
        return -1;
      }

      int8_t indice = indiceActual;

      for (uint8_t intento = 0;
          intento < MAX_ALARMAS;
          intento++) {

        indice += direccion;

        if (indice < 0) {
          indice = MAX_ALARMAS - 1;
        } else if (indice >= MAX_ALARMAS) {
          indice = 0;
        }

        if (alarmas[indice].activa == 1) {
          return indice;
        }
      }

      return -1;
    }
    void iniciarEliminacionAlarma() {
      pasoEliminarAlarma =
        PasoEliminarAlarma::SELECCIONAR;

      opcionConfirmacionEliminar = 0;

      indiceAlarmaEliminar =
        buscarSiguienteAlarmaActiva(-1, 1);

      cambiarEstado(
        EstadoSistema::ELIMINAR_ALARMA
      );
    }
    void copiarNombreVisible(
        const Alarma &alarma,
        char *destino
      ) {
      strncpy(
        destino,
        alarma.nombre,
        LONGITUD_NOMBRE
      );

      destino[LONGITUD_NOMBRE] = '\0';

      for (
        int8_t i = LONGITUD_NOMBRE - 1;
        i >= 0;
        i--
      ) {
        if (
          destino[i] == ' ' ||
          destino[i] == '\0'
        ) {
          destino[i] = '\0';
        } else {
          break;
        }
      }
    }
   

  // ============================================================
  // ESTADO ELIMINARA ALARMA
  // ============================================================
    void EstadoEliminarAlarma(
        EventosJoystick evento
      ) {
      switch (pasoEliminarAlarma) {

        // --------------------------------------------------------
        // SELECCIONAR ALARMA
        // --------------------------------------------------------
        case PasoEliminarAlarma::SELECCIONAR:

          if (indiceAlarmaEliminar < 0) {
            if (
              evento == EventosJoystick::IZQUIERDA ||
              evento == EventosJoystick::BOTON
            ) {
              opcionMenuPrincipal = 1;
              cambiarEstado(
                EstadoSistema::MENU_PRINCIPAL
              );
            }

            return;
          }

          if (evento == EventosJoystick::ARRIBA) {
            indiceAlarmaEliminar =
              buscarSiguienteAlarmaActiva(
                indiceAlarmaEliminar,
                -1
              );

            mostrarSeleccionEliminar();
            return;
          }

          if (evento == EventosJoystick::ABAJO) {
            indiceAlarmaEliminar =
              buscarSiguienteAlarmaActiva(
                indiceAlarmaEliminar,
                1
              );

            mostrarSeleccionEliminar();
            return;
          }

          if (evento == EventosJoystick::IZQUIERDA) {
            opcionMenuPrincipal = 1;

            cambiarEstado(
              EstadoSistema::MENU_PRINCIPAL
            );

            return;
          }

          if (evento == EventosJoystick::BOTON) {
            opcionConfirmacionEliminar = 1;

            pasoEliminarAlarma =
              PasoEliminarAlarma::CONFIRMAR;

            mostrarConfirmacionEliminar();
            return;
          }

          break;


        // --------------------------------------------------------
        // CONFIRMAR ELIMINACIÓN
        // --------------------------------------------------------
        case PasoEliminarAlarma::CONFIRMAR:

          if (
            evento == EventosJoystick::IZQUIERDA ||
            evento == EventosJoystick::DERECHA
          ) {
            opcionConfirmacionEliminar =
              opcionConfirmacionEliminar == 0
                ? 1
                : 0;

            mostrarConfirmacionEliminar();
            return;
          }

          if (evento != EventosJoystick::BOTON) {
            return;
          }

          // Cancelar
          if (opcionConfirmacionEliminar == 1) {
            pasoEliminarAlarma =
              PasoEliminarAlarma::SELECCIONAR;

            mostrarSeleccionEliminar();
            return;
          }

          // Eliminar
          if (
            indiceAlarmaEliminar >= 0 &&
            indiceAlarmaEliminar < MAX_ALARMAS
          ) {
            uint8_t indiceEliminado =
              static_cast<uint8_t>(
                indiceAlarmaEliminar
              );

            eliminarAlarma(indiceEliminado);

            indiceAlarmaEliminar =
              buscarSiguienteAlarmaActiva(
                indiceEliminado,
                1
              );

            pasoEliminarAlarma =
              PasoEliminarAlarma::RESULTADO;

            mostrarResultadoEliminar();
          }

          return;


        // --------------------------------------------------------
        // MENSAJE DESPUÉS DE ELIMINAR
        // --------------------------------------------------------
        case PasoEliminarAlarma::RESULTADO:

          if (evento == EventosJoystick::NINGUNO) {
            return;
          }

          if (indiceAlarmaEliminar < 0) {
            opcionMenuPrincipal = 1;

            cambiarEstado(
              EstadoSistema::MENU_PRINCIPAL
            );

            return;
          }

          pasoEliminarAlarma =
            PasoEliminarAlarma::SELECCIONAR;

          mostrarSeleccionEliminar();
          return;
      }
    }
    
  // ============================================================
  // AJUSTAR ALARMA
  // ============================================================
    void mostrarSeleccionDias() {
      lcd.noBlink();
      lcd.clear();

      char texto[20];

      if (opcionDia <= 6) {
        char nombreDia[10];
        strncpy_P(
          nombreDia,
          reinterpret_cast<PGM_P>(NOMBRES_DIAS[opcionDia]),
          sizeof(nombreDia) - 1
        );
        nombreDia[sizeof(nombreDia) - 1] = '\0';

        snprintf(texto, sizeof(texto), "Dia: %-10s", nombreDia);
        lcd.setCursor(0, 0);
        lcd.print(texto);

        lcd.setCursor(0, 1);
        bool elegido = (alarmaEnEdicion.dias & (1U << opcionDia)) != 0;
        lcd.print(elegido ? F("[X] Btn cambia  ") : F("[ ] Btn cambia  "));
      } else if (opcionDia == 7) {
        lcd.setCursor(0, 0);
        lcd.print(F("Todos los dias  "));
        lcd.setCursor(0, 1);
        lcd.print(
          alarmaEnEdicion.dias == 0x7F
            ? F("[X] Btn cambia  ")
            : F("[ ] Btn cambia  ")
        );
      } else {
        lcd.setCursor(0, 0);
        lcd.print(F(">Listo          "));

        uint8_t cantidad = 0;
        for (uint8_t i = 0; i < 7; i++) {
          if ((alarmaEnEdicion.dias & (1U << i)) != 0) {
            cantidad++;
          }
        }

        snprintf(texto, sizeof(texto), "%u dias          ",
                static_cast<unsigned int>(cantidad));
        texto[16] = '\0';
        lcd.setCursor(0, 1);
        lcd.print(texto);
      }
    }

    void procesarSeleccionDias(EventosJoystick evento) {
      if (evento == EventosJoystick::ARRIBA) {
        opcionDia = opcionDia == 0 ? 8 : opcionDia - 1;
        mostrarSeleccionDias();
        return;
      }

      if (evento == EventosJoystick::ABAJO) {
        opcionDia = opcionDia == 8 ? 0 : opcionDia + 1;
        mostrarSeleccionDias();
        return;
      }

      if (evento == EventosJoystick::IZQUIERDA) {
        cambiarEstado(EstadoSistema::MENU_PRINCIPAL);
        return;
      }

      if (evento != EventosJoystick::BOTON) {
        return;
      }

      if (opcionDia <= 6) {
        alarmaEnEdicion.dias ^= static_cast<uint8_t>(1U << opcionDia);
        mostrarSeleccionDias();
        return;
      }

      if (opcionDia == 7) {
        alarmaEnEdicion.dias =
          alarmaEnEdicion.dias == 0x7F ? 0x00 : 0x7F;
        mostrarSeleccionDias();
        return;
      }

      if (alarmaEnEdicion.dias == 0) {
        mostrarMensaje(F("Seleccione dia"), F("antes de seguir"));
        return;
      }

      pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_HORA;
      mostrarSeleccionHora();
    }

    void mostrarSeleccionHora() {
      lcd.noBlink();
      lcd.clear();

      char linea[17];
      snprintf(
        linea,
        sizeof(linea),
        "Hora: %02u:%02u %s",
        static_cast<unsigned int>(hora12Edicion),
        static_cast<unsigned int>(minutoEdicion),
        periodoPM ? "PM" : "AM"
      );

      lcd.setCursor(0, 0);
      lcd.print(linea);
      lcd.setCursor(0, 1);
      lcd.print(F("<> campo Btn OK "));

      const uint8_t posicionesCursor[3] = {6, 9, 12};
      lcd.setCursor(posicionesCursor[campoHora], 0);
      lcd.blink();
    }

    void procesarSeleccionHora(EventosJoystick evento) {
      if (evento == EventosJoystick::IZQUIERDA) {
        if (campoHora == 0) {
          pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_DIAS;
          mostrarSeleccionDias();
        } else {
          campoHora--;
          mostrarSeleccionHora();
        }
        return;
      }

      if (evento == EventosJoystick::DERECHA) {
        if (campoHora < 2) {
          campoHora++;
          mostrarSeleccionHora();
        }
        return;
      }

      if (evento == EventosJoystick::ARRIBA) {
        if (campoHora == 0) {
          hora12Edicion = hora12Edicion == 12 ? 1 : hora12Edicion + 1;
        } else if (campoHora == 1) {
          minutoEdicion = minutoEdicion == 59 ? 0 : minutoEdicion + 1;
        } else {
          periodoPM = !periodoPM;
        }

        mostrarSeleccionHora();
        return;
      }

      if (evento == EventosJoystick::ABAJO) {
        if (campoHora == 0) {
          hora12Edicion = hora12Edicion == 1 ? 12 : hora12Edicion - 1;
        } else if (campoHora == 1) {
          minutoEdicion = minutoEdicion == 0 ? 59 : minutoEdicion - 1;
        } else {
          periodoPM = !periodoPM;
        }

        mostrarSeleccionHora();
        return;
      }

      if (evento == EventosJoystick::BOTON) {
        alarmaEnEdicion.hora = convertirHora24(hora12Edicion, periodoPM);
        alarmaEnEdicion.minuto = minutoEdicion;

        pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_MEDICINA;
        mostrarSeleccionMedicina();
      }
    }

    // -------------------- MEDICINA --------------------
    void copiarNombrePredefinido(uint8_t indice, char *destino) {
      if (indice >= NUM_MEDICINAS) {
        destino[0] = '\0';
        return;
      }

      strncpy_P(
        destino,
        reinterpret_cast<PGM_P>(MEDICINAS[indice]),
        LONGITUD_NOMBRE
      );
      destino[LONGITUD_NOMBRE] = '\0';
    }

    void mostrarSeleccionMedicina() {
      lcd.noBlink();
      lcd.clear();

      char nombre[LONGITUD_NOMBRE + 1];
      copiarNombrePredefinido(opcionMedicina, nombre);

      char cabecera[17];
      snprintf(
        cabecera,
        sizeof(cabecera),
        "Medicina %u/%u",
        static_cast<unsigned int>(opcionMedicina + 1),
        static_cast<unsigned int>(NUM_MEDICINAS)
      );

      lcd.setCursor(0, 0);
      lcd.print(cabecera);
      lcd.setCursor(0, 1);
      lcd.print('>');
      lcd.print(nombre);

      for (uint8_t i = strlen(nombre) + 1; i < 16; i++) {
        lcd.print(' ');
      }
    }

    void procesarSeleccionMedicina(EventosJoystick evento) {
      if (evento == EventosJoystick::ARRIBA) {
        opcionMedicina =
          opcionMedicina == 0 ? NUM_MEDICINAS - 1 : opcionMedicina - 1;
        mostrarSeleccionMedicina();
        return;
      }

      if (evento == EventosJoystick::ABAJO) {
        opcionMedicina =
          opcionMedicina + 1 >= NUM_MEDICINAS ? 0 : opcionMedicina + 1;
        mostrarSeleccionMedicina();
        return;
      }

      if (evento == EventosJoystick::IZQUIERDA) {
        pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_HORA;
        mostrarSeleccionHora();
        return;
      }

      if (evento != EventosJoystick::BOTON) {
        return;
      }

      if (opcionMedicina == 0) {
        memset(alarmaEnEdicion.nombre, ' ', LONGITUD_NOMBRE);
        alarmaEnEdicion.nombre[LONGITUD_NOMBRE] = '\0';

        posicionNombre = 0;
        focoAccionesNombre = false;
        accionNombre = 0;

        pasoCrearAlarma = PasoCrearAlarma::EDITAR_NOMBRE;
        mostrarEditorNombre();
        return;
      }

      copiarNombrePredefinido(opcionMedicina, alarmaEnEdicion.nombre);
      pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_DOSIS;
      mostrarSeleccionDosis();
    }   
    // -------------------- EDITOR DEL NOMBRE --------------------
    bool nombreTieneLetras() {
      for (uint8_t i = 0; i < LONGITUD_NOMBRE; i++) {
        char caracter = alarmaEnEdicion.nombre[i];

        if (caracter >= 'A' && caracter <= 'Z') {
          return true;
        }
      }

      return false;
    }

    void mostrarEditorNombre() {
      char lineaNombre[17];
      lineaNombre[0] = '[';

      for (uint8_t i = 0; i < LONGITUD_NOMBRE; i++) {
        char caracter = alarmaEnEdicion.nombre[i];
        lineaNombre[i + 1] = caracter == ' ' ? '_' : caracter;
      }

      lineaNombre[15] = ']';
      lineaNombre[16] = '\0';

      lcd.noBlink();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(lineaNombre);
      lcd.setCursor(0, 1);
      lcd.print(F("SALIR    GUARDAR"));

      if (!focoAccionesNombre) {
        lcd.setCursor(posicionNombre + 1, 0);
      } else {
        lcd.setCursor(accionNombre == 0 ? 0 : 9, 1);
      }

      lcd.blink();
    }

    void procesarEditorNombre(EventosJoystick evento) {
      bool cambioPantalla = false;

      if (!focoAccionesNombre) {
        if (evento == EventosJoystick::IZQUIERDA && posicionNombre > 0) {
          posicionNombre--;
          cambioPantalla = true;
        } else if (
          evento == EventosJoystick::DERECHA &&
          posicionNombre < LONGITUD_NOMBRE - 1
        ) {
          posicionNombre++;
          cambioPantalla = true;
        } else if (evento == EventosJoystick::ABAJO) {
          focoAccionesNombre = true;
          cambioPantalla = true;
        } else if (evento == EventosJoystick::BOTON) {
          filaLetra = 0;
          columnaLetra = 0;
          pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_LETRA;
          mostrarSelectorLetra();
          return;
        }
      } else {
        if (
          evento == EventosJoystick::IZQUIERDA ||
          evento == EventosJoystick::DERECHA
        ) {
          accionNombre = accionNombre == 0 ? 1 : 0;
          cambioPantalla = true;
        } else if (evento == EventosJoystick::ARRIBA) {
          focoAccionesNombre = false;
          cambioPantalla = true;
        } else if (evento == EventosJoystick::BOTON) {
          if (accionNombre == 0) {
            pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_MEDICINA;
            mostrarSeleccionMedicina();
            return;
          }

          if (!nombreTieneLetras()) {
            mostrarMensaje(F("Nombre vacio"), F("Agregue letras"));
            return;
          }

          pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_DOSIS;
          mostrarSeleccionDosis();
          return;
        }
      }

      if (cambioPantalla) {
        mostrarEditorNombre();
      }
    }

    // -------------------- SELECTOR DE LETRA --------------------
    void mostrarSelectorLetra() {
      lcd.noBlink();
      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print(F("ABCDEFGHIJKLM<_"));

      lcd.setCursor(0, 1);
      lcd.print(F("NOPQRSTUVWXYZ   "));

      lcd.setCursor(columnaLetra, filaLetra);
      lcd.blink();
    }

    char obtenerLetraSeleccionada() {
      static const char FILA_SUPERIOR[] = "ABCDEFGHIJKLM<_";
      static const char FILA_INFERIOR[] = "NOPQRSTUVWXYZ";

      if (filaLetra == 0) {
        return FILA_SUPERIOR[columnaLetra];
      }

      return FILA_INFERIOR[columnaLetra];
    }

    void procesarSelectorLetra(EventosJoystick evento) {
      bool cambioPantalla = false;

      if (evento == EventosJoystick::IZQUIERDA && columnaLetra > 0) {
        columnaLetra--;
        cambioPantalla = true;
      } else if (
        evento == EventosJoystick::DERECHA &&
        columnaLetra + 1 < COLUMNAS_FILA_LETRAS[filaLetra]
      ) {
        columnaLetra++;
        cambioPantalla = true;
      } else if (
        evento == EventosJoystick::ARRIBA &&
        filaLetra == 1
      ) {
        filaLetra = 0;
        cambioPantalla = true;
      } else if (
        evento == EventosJoystick::ABAJO &&
        filaLetra == 0
      ) {
        filaLetra = 1;

        if (columnaLetra >= COLUMNAS_FILA_LETRAS[filaLetra]) {
          columnaLetra = COLUMNAS_FILA_LETRAS[filaLetra] - 1;
        }

        cambioPantalla = true;
      } else if (evento == EventosJoystick::BOTON) {
        char seleccion = obtenerLetraSeleccionada();

        if (seleccion == '<' || seleccion == '_') {
          alarmaEnEdicion.nombre[posicionNombre] = ' ';
        } else {
          alarmaEnEdicion.nombre[posicionNombre] = seleccion;

          if (posicionNombre < LONGITUD_NOMBRE - 1) {
            posicionNombre++;
          }
        }

        pasoCrearAlarma = PasoCrearAlarma::EDITAR_NOMBRE;
        mostrarEditorNombre();
        return;
      }

      if (cambioPantalla) {
        mostrarSelectorLetra();
      }
    }

    // -------------------- DOSIS --------------------
    void copiarDosis(uint8_t indice, char *destino) {
      if (indice >= NUM_DOSIS) {
        destino[0] = '\0';
        return;
      }

      strncpy_P(
        destino,
        reinterpret_cast<PGM_P>(DOSIS[indice]),
        16
      );
      destino[16] = '\0';
    }

    void mostrarSeleccionDosis() {
      lcd.noBlink();
      lcd.clear();

      char dosis[17];
      copiarDosis(opcionDosis, dosis);

      char cabecera[17];
      snprintf(
        cabecera,
        sizeof(cabecera),
        "Dosis %u/%u",
        static_cast<unsigned int>(opcionDosis + 1),
        static_cast<unsigned int>(NUM_DOSIS)
      );

      lcd.setCursor(0, 0);
      lcd.print(cabecera);
      lcd.setCursor(0, 1);
      lcd.print('>');
      lcd.print(dosis);

      for (uint8_t i = strlen(dosis) + 1; i < 16; i++) {
        lcd.print(' ');
      }
    }

    void procesarSeleccionDosis(EventosJoystick evento) {
      if (evento == EventosJoystick::ARRIBA) {
        opcionDosis = opcionDosis == 0 ? NUM_DOSIS - 1 : opcionDosis - 1;
        mostrarSeleccionDosis();
        return;
      }

      if (evento == EventosJoystick::ABAJO) {
        opcionDosis = opcionDosis + 1 >= NUM_DOSIS ? 0 : opcionDosis + 1;
        mostrarSeleccionDosis();
        return;
      }

      if (evento == EventosJoystick::IZQUIERDA) {
        pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_MEDICINA;
        mostrarSeleccionMedicina();
        return;
      }

      if (evento == EventosJoystick::BOTON) {
        alarmaEnEdicion.dosisId = opcionDosis;
        pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_GAVETA;
        mostrarSeleccionGaveta();
      }
    }

    // -------------------- GAVETA --------------------
    void mostrarSeleccionGaveta() {
      lcd.noBlink();
      lcd.clear();

      char linea[17];
      snprintf(
        linea,
        sizeof(linea),
        "Gaveta: %u de %u",
        static_cast<unsigned int>(gavetaEdicion + 1),
        static_cast<unsigned int>(NUM_GAVETAS)
      );

      lcd.setCursor(0, 0);
      lcd.print(linea);
      lcd.setCursor(0, 1);
      lcd.print(F("Btn OK  < atras "));
    }

    void procesarSeleccionGaveta(EventosJoystick evento) {
      if (evento == EventosJoystick::ARRIBA) {
        gavetaEdicion =
          gavetaEdicion == 0 ? NUM_GAVETAS - 1 : gavetaEdicion - 1;
        mostrarSeleccionGaveta();
        return;
      }

      if (evento == EventosJoystick::ABAJO) {
        gavetaEdicion =
          gavetaEdicion + 1 >= NUM_GAVETAS ? 0 : gavetaEdicion + 1;
        mostrarSeleccionGaveta();
        return;
      }

      if (evento == EventosJoystick::IZQUIERDA) {
        pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_DOSIS;
        mostrarSeleccionDosis();
        return;
      }

      if (evento == EventosJoystick::BOTON) {
        alarmaEnEdicion.gaveta = gavetaEdicion;
        opcionConfirmacion = 0;
        pasoCrearAlarma = PasoCrearAlarma::CONFIRMAR;
        mostrarConfirmacionAlarma();
      }
    }

    // -------------------- CONFIRMACION --------------------
    void mostrarConfirmacionAlarma() {
      lcd.noBlink();
      lcd.clear();

      char nombreVisible[17];
      strncpy(nombreVisible, alarmaEnEdicion.nombre, 16);
      nombreVisible[16] = '\0';

      // El nombre personalizado se guarda con espacios internos.
      // Para la pantalla reemplazamos espacios finales por '\0'.
      for (int8_t i = 15; i >= 0; i--) {
        if (nombreVisible[i] == ' ' || nombreVisible[i] == '\0') {
          nombreVisible[i] = '\0';
        } else {
          break;
        }
      }

      lcd.setCursor(0, 0);
      lcd.print(nombreVisible);
      for (uint8_t i = strlen(nombreVisible); i < 16; i++) {
        lcd.print(' ');
      }

      lcd.setCursor(0, 1);
      if (opcionConfirmacion == 0) {
        lcd.print(F(">Guardar  Volver"));
      } else {
        lcd.print(F(" Guardar >Volver"));
      }
    }

    int8_t buscarEspacioLibre() {
      for (uint8_t i = 0; i < MAX_ALARMAS; i++) {
        if (!alarmas[i].activa) {
          return static_cast<int8_t>(i);
        }
      }

      return -1;
    }

    void procesarConfirmacionAlarma(EventosJoystick evento) {
      if (alarmaGuardadaEsperandoSalida) {
        if (evento != EventosJoystick::NINGUNO) {
          opcionMenuPrincipal = 0;
          cambiarEstado(EstadoSistema::MENU_PRINCIPAL);
        }
        return;
      }

      if (
        evento == EventosJoystick::IZQUIERDA ||
        evento == EventosJoystick::DERECHA
      ) {
        opcionConfirmacion = opcionConfirmacion == 0 ? 1 : 0;
        mostrarConfirmacionAlarma();
        return;
      }

      if (evento != EventosJoystick::BOTON) {
        return;
      }

      if (opcionConfirmacion == 1) {
        pasoCrearAlarma = PasoCrearAlarma::SELECCIONAR_GAVETA;
        mostrarSeleccionGaveta();
        return;
      }

      int8_t indiceLibre = buscarEspacioLibre();

      if (indiceLibre < 0) {
        mostrarMensaje(F("Memoria llena"), F("Elimine alarma"));
        return;
      } 

      alarmas[indiceLibre] = alarmaEnEdicion;
      guardarAlarmas();

      Serial.print(F("Alarma guardada en indice "));
      Serial.println(indiceLibre);

      imprimirAlarmas();

      alarmaGuardadaEsperandoSalida = true;
      mostrarMensaje(F("Alarma guardada"), F("Boton: menu"));
    }

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
    bool alarmaCoincideAhora(const Alarma &alarma) {
      if (alarma.activa != 1) {
        return false;
      }

      uint8_t diaActual = fechaHoraActual.dayOfTheWeek();

      bool correspondeDia =
        (alarma.dias & static_cast<uint8_t>(1U << diaActual)) != 0;

      bool correspondeHora =
        alarma.hora == fechaHoraActual.hour();

      bool correspondeMinuto =
        alarma.minuto == fechaHoraActual.minute();

      return correspondeDia &&
            correspondeHora &&
            correspondeMinuto;
    }
    void actualizarAlarmas() {
      if (!rtcDisponible) {
        return;
      }

      uint32_t minutoActual =
        fechaHoraActual.unixtime() / 60UL;

      // No volver a procesar el mismo minuto.
      if (minutoActual != minutoRTCProcesado) {
        minutoRTCProcesado = minutoActual;

        for (uint8_t i = 0; i < MAX_ALARMAS; i++) {
          if (alarmaCoincideAhora(alarmas[i])) {
            alarmasPendientes |=
              static_cast<uint8_t>(1U << i);

            Serial.print(F("Alarma pendiente: "));
            Serial.println(i);
          }
        }
      }

      // Las alarmas tienen prioridad sobre el menú.
      if (
        estadoActual != EstadoSistema::ALARMA_ACTIVA &&
        estadoActual != EstadoSistema::MOSTRAR_DOSIS &&
        alarmasPendientes != 0
      ) {
        activarSiguienteAlarmaPendiente();
      }
    }
    void activarSiguienteAlarmaPendiente() {
      for (uint8_t i = 0; i < MAX_ALARMAS; i++) {
        uint8_t mascara =
          static_cast<uint8_t>(1U << i);

        if ((alarmasPendientes & mascara) == 0) {
          continue;
        }

        // Retirar la alarma de la cola.
        alarmasPendientes &=
          static_cast<uint8_t>(~mascara);

        // Protección por si fue eliminada.
        if (alarmas[i].activa != 1) {
          continue;
        }

        indiceAlarmaActiva = static_cast<int8_t>(i);
        cambiarEstado(EstadoSistema::ALARMA_ACTIVA);
        return;
      }
    }



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

      uint8_t hora24 = fechaHoraActual.hour();
      uint8_t hora12 = hora24 % 12;

      if (hora12 == 0) {
        hora12 = 12;
      }

      const char* periodo = hora24 >= 12 ? "PM" : "AM";

      snprintf(
        lineaHora,
        sizeof(lineaHora),
        "Hora:  %02u:%02u %s ",
        static_cast<unsigned int>(hora12),
        static_cast<unsigned int>(fechaHoraActual.minute()),
        periodo
      );

      snprintf(
        lineaFecha,
        sizeof(lineaFecha),
        "Fecha:%02u/%02u/%04u      ",
        static_cast<unsigned int>(fechaHoraActual.day()),
        static_cast<unsigned int>(fechaHoraActual.month()),
        static_cast<unsigned int>(fechaHoraActual.year())
      );

      lineaHora[16] = '\0';
      lineaFecha[16] = '\0';

      lcd.setCursor(0,0); 
      lcd.print(lineaHora);

      lcd.setCursor(0,1); 
      lcd.print(lineaFecha);
    }

    void mostrarAjusteReloj() {
      lcd.noBlink();
      lcd.clear();

      uint8_t campo =
        static_cast<uint8_t>(
          campoAjusteReloj
        );

      // ----------------------------------------------------------
      // CAMPOS DE FECHA
      // ----------------------------------------------------------
      if (
        campo <= static_cast<uint8_t>(
          CampoAjusteReloj::ANIO
        )
      ) {
        char lineaFecha[17];

        snprintf(
          lineaFecha,
          sizeof(lineaFecha),
          "Fecha %02u/%02u/%04u",
          static_cast<unsigned int>(
            diaRelojEdicion
          ),
          static_cast<unsigned int>(
            mesRelojEdicion
          ),
          static_cast<unsigned int>(
            anioRelojEdicion
          )
        );

        lcd.setCursor(0, 0);
        lcd.print(lineaFecha);

        lcd.setCursor(0, 1);
        lcd.print(F("<>campo ^v mod  "));

        const uint8_t CURSORES_FECHA[3] = {
          6,   // día
          9,   // mes
          12   // año
        };

        lcd.setCursor(
          CURSORES_FECHA[campo],
          0
        );

        lcd.blink();
        return;
      }

      // ----------------------------------------------------------
      // CAMPOS DE HORA
      // ----------------------------------------------------------
      if (
        campo <= static_cast<uint8_t>(
          CampoAjusteReloj::PERIODO
        )
      ) {
        char lineaHora[17];

        snprintf(
          lineaHora,
          sizeof(lineaHora),
          "Hora  %02u:%02u %s",
          static_cast<unsigned int>(
            hora12RelojEdicion
          ),
          static_cast<unsigned int>(
            minutoRelojEdicion
          ),
          periodoPMRelojEdicion
            ? "PM"
            : "AM"
        );

        lcd.setCursor(0, 0);
        lcd.print(lineaHora);

        lcd.setCursor(0, 1);
        lcd.print(F("<>campo ^v mod  "));

        const uint8_t CURSORES_HORA[3] = {
          6,   // hora
          9,   // minuto
          12   // AM/PM
        };

        uint8_t indiceCursor =
          campo -
          static_cast<uint8_t>(
            CampoAjusteReloj::HORA
          );

        lcd.setCursor(
          CURSORES_HORA[indiceCursor],
          0
        );

        lcd.blink();
        return;
      }

      // ----------------------------------------------------------
      // CONFIRMACIÓN
      // ----------------------------------------------------------
      uint8_t hora24 =
        convertirHora24(
          hora12RelojEdicion,
          periodoPMRelojEdicion
        );

      char resumen[17];

      snprintf(
        resumen,
        sizeof(resumen),
        "%02u/%02u/%02u %02u:%02u",
        static_cast<unsigned int>(
          diaRelojEdicion
        ),
        static_cast<unsigned int>(
          mesRelojEdicion
        ),
        static_cast<unsigned int>(
          anioRelojEdicion % 100
        ),
        static_cast<unsigned int>(hora24),
        static_cast<unsigned int>(
          minutoRelojEdicion
        )
      );

      lcd.setCursor(0, 0);
      lcd.print(resumen);

      for (
        uint8_t i = strlen(resumen);
        i < 16;
        i++
      ) {
        lcd.print(' ');
      }

      lcd.setCursor(0, 1);

      if (opcionConfirmacionReloj == 0) {
        lcd.print(F(">Guardar  Salir "));
      } else {
        lcd.print(F(" Guardar >Salir "));
      }
    }
    
    void mostrarMenuPrincipal() {
      lcd.noBlink();

      if (opcionMenuPrincipal == 0) {
        lcd.setCursor(0, 0);
        lcd.print(F(">Crear alarma   "));

        lcd.setCursor(0, 1);
        lcd.print(F(" Eliminar alarma"));

      } else if (opcionMenuPrincipal == 1) {
        lcd.setCursor(0, 0);
        lcd.print(F(" Crear alarma   "));

        lcd.setCursor(0, 1);
        lcd.print(F(">Eliminar alarma"));

      } else {
        lcd.setCursor(0, 0);
        lcd.print(F(" Eliminar alarma"));

        lcd.setCursor(0, 1);
        lcd.print(F(">Ajustar reloj  "));
      }
    }

    void mostrarErrorRTC(){
      lcd.noBlink();
      lcd.setCursor(0,0);
      lcd.print(F("ERROR DEL RELOJ"));

      lcd.setCursor(0,1);
      lcd.print(F("Boton: reint."));
      
    }

    void mostrarMensaje(
        const __FlashStringHelper *linea1,
        const __FlashStringHelper *linea2
      ) {
      lcd.noBlink();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(linea1);
      lcd.setCursor(0, 1);
      lcd.print(linea2);
    }

    void mostrarAlarmaActiva() {
      if (
        indiceAlarmaActiva < 0 ||
        indiceAlarmaActiva >= MAX_ALARMAS
      ) {
        return;
      }

      const Alarma &alarma = alarmas[indiceAlarmaActiva];

      lcd.noBlink();
      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print(F("TOMAR AHORA"));

      lcd.setCursor(0, 1);
      lcd.print(alarma.nombre);
    }
    void mostrarDosisActiva() {
      if (
        indiceAlarmaActiva < 0 ||
        indiceAlarmaActiva >= MAX_ALARMAS
      ) {
        return;
      }

      char dosis[17];

      copiarDosis(
        alarmas[indiceAlarmaActiva].dosisId,
        dosis
      );

      lcd.noBlink();
      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print(F("DOSIS A TOMAR:"));

      lcd.setCursor(0, 1);
      lcd.print(dosis);
    }

    void mostrarSeleccionEliminar() {
      lcd.noBlink();
      lcd.clear();

      if (
        indiceAlarmaEliminar < 0 ||
        indiceAlarmaEliminar >= MAX_ALARMAS
      ) {
        lcd.setCursor(0, 0);
        lcd.print(F("Sin alarmas     "));

        lcd.setCursor(0, 1);
        lcd.print(F("Izq: regresar   "));
        return;
      }

      const Alarma &alarma =
        alarmas[indiceAlarmaEliminar];

      char nombreVisible[LONGITUD_NOMBRE + 1];
      copiarNombreVisible(
        alarma,
        nombreVisible
      );

      lcd.setCursor(0, 0);
      lcd.print(nombreVisible);

      for (
        uint8_t i = strlen(nombreVisible);
        i < 16;
        i++
      ) {
        lcd.print(' ');
      }

      uint8_t hora12 = alarma.hora % 12;

      if (hora12 == 0) {
        hora12 = 12;
      }

      const char *periodo =
        alarma.hora >= 12 ? "PM" : "AM";

      char linea2[17];

      snprintf(
        linea2,
        sizeof(linea2),
        "%02u:%02u %s G%u Btn",
        static_cast<unsigned int>(hora12),
        static_cast<unsigned int>(alarma.minuto),
        periodo,
        static_cast<unsigned int>(
          alarma.gaveta + 1
        )
      );

      lcd.setCursor(0, 1);
      lcd.print(linea2);

      for (
        uint8_t i = strlen(linea2);
        i < 16;
        i++
      ) {
        lcd.print(' ');
      }
    }
   
    void mostrarResultadoEliminar() {
      mostrarMensaje(
        F("Alarma eliminada"),
        indiceAlarmaEliminar < 0
          ? F("No quedan alarm.")
          : F("Btn: continuar")
      );
    }

    void mostrarConfirmacionEliminar() {
      lcd.noBlink();
      lcd.clear();

      if (
        indiceAlarmaEliminar < 0 ||
        indiceAlarmaEliminar >= MAX_ALARMAS
      ) {
        mostrarSeleccionEliminar();
        return;
      }

      char nombreVisible[LONGITUD_NOMBRE + 1];

      copiarNombreVisible(
        alarmas[indiceAlarmaEliminar],
        nombreVisible
      );

      lcd.setCursor(0, 0);
      lcd.print(nombreVisible);

      for (
        uint8_t i = strlen(nombreVisible);
        i < 16;
        i++
      ) {
        lcd.print(' ');
      }

      lcd.setCursor(0, 1);

      if (opcionConfirmacionEliminar == 0) {
        lcd.print(F(">Eliminar Cancel"));
      } else {
        lcd.print(F(" Eliminar>Cancel"));
      }
    }

  // ============================================================
  // EEPROM
  // ============================================================
    void imprimirDiasAlarma(uint8_t dias) {
      if (dias == 0) {
        Serial.print(F("Ninguno"));
        return;
      }

      if (dias == 0x7F) {
        Serial.print(F("Todos"));
        return;
      }

      const char *abreviaturas[] = {
        "Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"
      };

      bool primerDia = true;

      for (uint8_t i = 0; i < 7; i++) {
        if ((dias & (1U << i)) != 0) {
          if (!primerDia) {
            Serial.print(',');
          }

          Serial.print(abreviaturas[i]);
          primerDia = false;
        }
      }
    }
    void imprimirAlarmas() {
      Serial.println();
      Serial.println(F("========================================"));
      Serial.println(F("LISTA DE ALARMAS GUARDADAS"));
      Serial.println(F("========================================"));

      uint8_t cantidadActivas = 0;

      for (uint8_t i = 0; i < MAX_ALARMAS; i++) {
        Serial.print(F("Posicion "));
        Serial.print(i);
        Serial.print(F(": "));

        if (alarmas[i].activa != 1) {
          Serial.println(F("LIBRE"));
          continue;
        }

        cantidadActivas++;

        char nombreVisible[LONGITUD_NOMBRE + 1];
        strncpy(
          nombreVisible,
          alarmas[i].nombre,
          LONGITUD_NOMBRE
        );
        nombreVisible[LONGITUD_NOMBRE] = '\0';

        // Eliminar espacios al final del nombre.
        for (int8_t j = LONGITUD_NOMBRE - 1; j >= 0; j--) {
          if (
            nombreVisible[j] == ' ' ||
            nombreVisible[j] == '\0'
          ) {
            nombreVisible[j] = '\0';
          } else {
            break;
          }
        }

        char dosisVisible[17];
        copiarDosis(alarmas[i].dosisId, dosisVisible);

        Serial.println(F("OCUPADA"));

        Serial.print(F("  Nombre: "));
        Serial.println(nombreVisible);

        Serial.print(F("  Dias: "));
        imprimirDiasAlarma(alarmas[i].dias);
        Serial.println();

        Serial.print(F("  Hora: "));

        if (alarmas[i].hora < 10) {
          Serial.print('0');
        }

        Serial.print(alarmas[i].hora);
        Serial.print(':');

        if (alarmas[i].minuto < 10) {
          Serial.print('0');
        }

        Serial.println(alarmas[i].minuto);

        Serial.print(F("  Dosis: "));
        Serial.println(dosisVisible);

        Serial.print(F("  Gaveta: "));
        Serial.println(alarmas[i].gaveta + 1);

        Serial.println(F("----------------------------------------"));
      }

      Serial.print(F("Alarmas activas: "));
      Serial.print(cantidadActivas);
      Serial.print('/');
      Serial.println(MAX_ALARMAS);

      Serial.println(F("========================================"));
      Serial.println();
    }
    
    
    void cargarAlarmas() {
      uint16_t firmaLeida = 0;
      EEPROM.get(DIRECCION_FIRMA, firmaLeida);

      if (firmaLeida != FIRMA_EEPROM) {
        Serial.println(F("EEPROM: formato nuevo, reiniciando"));

        memset(alarmas, 0, sizeof(alarmas));

        EEPROM.put(DIRECCION_FIRMA, FIRMA_EEPROM);
        EEPROM.put(DIRECCION_ALARMAS, alarmas);
        return;
      }

      EEPROM.get(DIRECCION_ALARMAS, alarmas);

      bool huboCorrecciones = false;

      for (uint8_t i = 0; i < MAX_ALARMAS; i++) {
        if (!alarmaAlmacenadaValida(alarmas[i])) {
          Serial.print(F("EEPROM: alarma corrupta en posicion "));
          Serial.println(i);

          memset(&alarmas[i], 0, sizeof(Alarma));
          huboCorrecciones = true;
        }
      }

      if (huboCorrecciones) {
        guardarAlarmas();
      }

      uint8_t cantidadActivas = 0;

      for (uint8_t i = 0; i < MAX_ALARMAS; i++) {
        if (alarmas[i].activa == 1) {
          cantidadActivas++;
        }
      }

      Serial.print(F("Alarmas cargadas: "));
      Serial.println(cantidadActivas);
    }

    bool alarmaAlmacenadaValida(const Alarma &alarma) {
      // Una posición desocupada es válida.
      if (alarma.activa == 0) {
        return true;
      }

      // El único valor válido para una alarma activa es 1.
      if (alarma.activa != 1) {
        return false;
      }

      // Debe tener al menos un día y solamente usar los bits 0-6.
      if (alarma.dias == 0 || (alarma.dias & 0x80) != 0) {
        return false;
      }

      if (alarma.hora > 23) {
        return false;
      }

      if (alarma.minuto > 59) {
        return false;
      }

      if (alarma.dosisId >= NUM_DOSIS) {
        return false;
      }

      if (alarma.gaveta >= NUM_GAVETAS) {
        return false;
      }

      // El nombre debe finalizar correctamente.
      if (alarma.nombre[LONGITUD_NOMBRE] != '\0') {
        return false;
      }

      return true;
    }

    void guardarAlarmas() {
      EEPROM.put(DIRECCION_FIRMA, FIRMA_EEPROM);
      EEPROM.put(DIRECCION_ALARMAS, alarmas);
    }

    void eliminarAlarma(uint8_t indice) {
      if (indice >= MAX_ALARMAS) {
        return;
      }

      memset(
        &alarmas[indice],
        0,
        sizeof(Alarma)
      );

      guardarAlarmas();

      // Retirar también cualquier activación pendiente.
      alarmasPendientes &=
        static_cast<uint8_t>(~(1U << indice));

      Serial.print(F("Alarma eliminada: "));
      Serial.println(indice);

      imprimirAlarmas();
    }

    void borrarTodasLasAlarmas() {
      memset(alarmas, 0, sizeof(alarmas));
      guardarAlarmas();

      Serial.println(F("Todas las alarmas fueron eliminadas"));
      imprimirAlarmas();
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
      servoCampana.write(servoEnPosicionDerecha ? 110 : 70);
    }

    void detenerCampana() {
      if (!servoCampana.attached()) {
        return;
      }

      servoCampana.write(ANGULO_CENTRO_SERVO);
      delay(TIEMPO_CENTRADO_SERVO_MS);

      servoCampana.detach();
      servoEnPosicionDerecha = false;
    }
  // ============================================================
  // CONTROL DE LOS LEDS
  // ============================================================
    void escribirLeds(uint8_t patron) {
      digitalWrite(PIN_595_LATCH, LOW);

      shiftOut(
        PIN_595_DATOS,
        PIN_595_RELOJ,
        MSBFIRST,
        patron
      );

      digitalWrite(PIN_595_LATCH, HIGH);
    }

    void encenderGaveta(uint8_t gaveta) {
      if (gaveta >= NUM_GAVETAS) {
        apagarTodasLasGavetas();
        return;
      }

      uint8_t patron = static_cast<uint8_t>(1U << gaveta);
      escribirLeds(patron);
    }

    void apagarTodasLasGavetas() {
      escribirLeds(0x00);
    }
