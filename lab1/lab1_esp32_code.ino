// Definición de pines según la guía
const int pinDAC = 25; // GPIO25 para la salida DAC real
const int pinPWM = 27; // GPIO27 para la salida PWM

void setup() {
  // Inicializar comunicación con el Monitor Serial
  Serial.begin(115200);
  
  // Configurar la salida PWM a 5000 Hz de frecuencia portadora y 8 bits de resolución
  // Se utiliza el periférico LEDC como especifica la guía
  ledcAttach(pinPWM, 5000, 8); 
  
  Serial.println("ESP32 Listo.");
  Serial.println("Ingrese un voltaje objetivo (0 a 3.3 V):");
}

void loop() {
  // Verificar si hay datos disponibles en el Monitor Serial
  if (Serial.available() > 0) {
    
    // Leer el voltaje objetivo ingresado
    float voltajeObjetivo = Serial.parseFloat();
    
    // Limpiar el salto de línea o caracteres extra en el buffer
    while(Serial.available() > 0) {
      Serial.read();
    }
    
    // Limitar el voltaje ingresado al rango válido del DAC (0 a 3.3 V)
    if (voltajeObjetivo < 0.0) {
      voltajeObjetivo = 0.0;
    } else if (voltajeObjetivo > 3.3) {
      voltajeObjetivo = 3.3;
    }
    
    // Convertir el voltaje a un código de 8 bits (0-255)
    // Se usa la misma escala para ambos métodos
    int codigo = (voltajeObjetivo / 3.3) * 255;
    
    // Entregar el código por el DAC interno (GPIO25)
    dacWrite(pinDAC, codigo);
    
    // Entregar el código simultáneamente por la salida PWM (GPIO27)
    ledcWrite(pinPWM, codigo);
    
    // Mostrar en el Monitor Serial el voltaje ingresado y el código calculado
    Serial.print("Voltaje ingresado: ");
    Serial.print(voltajeObjetivo);
    Serial.print(" V  -->  Código de 8 bits: ");
    Serial.println(codigo);
  }
}
