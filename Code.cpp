
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include "Audio.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiClientSecure.h>

// WiFi
const char* ssid = "Ankush";
const char* password = "123456789";

// Groq API
String GROQ_API_KEY = "Your Groq API Key ";


// MAX30100
PulseOximeter pox;
float heartRate = 0;
float spo2 = 0;
uint32_t tsLastReport = 0;
#define REPORTING_PERIOD_MS 250
bool pulseActive = false;

void onBeatDetected() { Serial.println("Beat Detected!"); }

// MAX98357
#define I2S_DOUT 27
#define I2S_BCLK 26
#define I2S_LRC 25
Audio audio;
bool speaking = false;

// DS18B20
#define ONE_WIRE_BUS 15
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float tempC = 0;
String healthStatus = "Unknown";

// WebServer
WebServer server(80);

// ---------------- HTML Dashboard ----------------
String webpage = R"====(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>NEXA AI Health Dashboard</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jspdf/2.5.1/jspdf.umd.min.js"></script>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:Segoe UI;}
body{background:linear-gradient(135deg,#0f2027,#203a43,#2c5364);color:white;min-height:100vh;}
header{text-align:center;font-size:28px;padding:20px;background:rgba(0,0,0,0.3);backdrop-filter:blur(10px);}
.container{width:95%;max-width:1300px;margin:auto;margin-top:30px;display:grid;grid-template-columns:2fr 1fr;gap:25px;}
.dashboard{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:25px;}
.card{background:rgba(255,255,255,0.08);border-radius:18px;padding:25px;text-align:center;box-shadow:0 10px 25px rgba(0,0,0,0.4);backdrop-filter:blur(12px);}
.value{font-size:40px;margin-top:10px;font-weight:bold;}
.unit{opacity:0.7;}
#heart{font-size:70px;color:red;animation:beat 1s infinite;}
@keyframes beat{0%{transform:scale(1)}50%{transform:scale(1.25)}100%{transform:scale(1)}}
canvas{width:100%;height:170px;}
.status{margin-top:15px;padding:8px 15px;border-radius:20px;display:inline-block;}
.good{background:#00c853;}
.wait{background:#ff9800;}
.aiBox{background:rgba(255,255,255,0.1);padding:30px;border-radius:20px;text-align:center;backdrop-filter:blur(15px);}
#face{font-size:80px;margin-bottom:10px;}
button{padding:12px 25px;border:none;border-radius:50px;background:#00ffd5;cursor:pointer;font-size:16px;font-weight:bold;margin-top:15px;}
.answer{margin-top:15px;background:white;color:#333;padding:15px;border-radius:10px;min-height:60px;}
footer{margin-top:30px;text-align:center;opacity:0.7;}
#userProfile{display:flex;justify-content:center;gap:15px;flex-wrap:wrap;margin-top:10px;}
#userProfile label{display:flex;flex-direction:column;font-size:14px;color:#fff;}
#userProfile input{margin-top:5px;padding:6px 10px;border-radius:8px;border:none;font-size:14px;width:80px;}
</style>
</head>
<body>
<header>NEXA AI Health Monitoring Dashboard</header>
<div class="container">

<div class="dashboard">

<div class="card">
<h2>Temperature</h2>
<div class="value" id="temp">--</div>
<div class="unit">°C</div>
<button onclick="checkTemp()">🌡 Check Temperature</button>
</div>

<div class="card">
<div id="heart">❤</div>
<h2>Heart Rate</h2>
<div class="value" id="bpm">--</div>
<div class="unit">BPM</div>
</div>

<div class="card">
<h2>SpO2</h2>
<div class="value" id="spo2">--</div>
<div class="unit">%</div>
</div>

<div class="card">
<h2>Heart Activity</h2>
<canvas id="chart"></canvas>
<div id="status" class="status wait">Place finger on sensor</div>
</div>

<!-- BMI CALCULATOR -->
<div class="card">
<h2>BMI</h2>
<div class="value" id="bmi">--</div>
<div class="unit">Body Mass Index</div>
<button onclick="calculateBMI()">Calculate BMI</button>
</div>

<!-- HEALTH STATUS -->
<div class="card">
<h2>Health Status</h2>
<div class="value" id="healthStatus">--</div>
<div class="unit">AI Analysis</div>
</div>

</div>

<!-- Right AI & info panel -->
<div class="aiBox">
<div id="face">🙂</div>
<h2>NEXA AI Assistant</h2>

<button onclick="record()">🎤 Ask Question</button>

<div class="answer" id="answer">Hello! I am Nexa created by Ankush. Ask me anything.</div>

<button onclick="getHealthAdvice()">🩺 Get Health Advice</button>

<div class="answer" id="healthAdvice">Health advice will appear here based on your latest readings.</div>

<button onclick="checkPulseWithProfile()">❤️ Check Pulse & SpO₂</button>


<button onclick="downloadReport()">📄 Download Health Report</button>

<div id="userProfile">
<label>Age<input type="number" id="age" value="25"></label>
<label>Weight (kg)<input type="number" id="weight" value="70"></label>
<label>Height (cm)<input type="number" id="height" value="170"></label>
</div>
</div>
</div>

<footer>ESP32 AI Health Monitor • Created by Ankush</footer>

<script>
let ctx=document.getElementById('chart').getContext('2d')
let chart=new Chart(ctx,{type:'line',data:{labels:[],datasets:[{data:[],borderColor:'#00ffe1',borderWidth:3,fill:false,tension:0.4}]},options:{animation:false,plugins:{legend:{display:false}},scales:{x:{display:false},y:{min:40,max:120}}}})

let pulseInterval;

function fetchData(){
  fetch('/data').then(res=>res.json()).then(data=>{
    if(data.valid){
      let hrVal = Number(data.hr);
      let spo2Val = Number(data.spo2);
      document.getElementById("bpm").innerText = hrVal.toFixed(0);
      document.getElementById("spo2").innerText = spo2Val.toFixed(0);

      if(data.health){
  document.getElementById("healthStatus").innerText = data.health;
} 
      document.getElementById("status").innerText = "Finger Detected";
      document.getElementById("status").className = "status good";

      chart.data.labels.push("");
      chart.data.datasets[0].data.push(hrVal);

      if(chart.data.labels.length > 30){
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
      }

      chart.update();
    }
  })
}


function checkTemp(){
  fetch('/temperature')
  .then(res=>res.text())
  .then(data=>{
    document.getElementById("temp").innerText=data;
  });
}

function getHealthAdvice(){
  fetch('/healthAdvice')
  .then(res=>res.text())
  .then(data=>{
    document.getElementById("healthAdvice").innerText=data;
  });
}

function checkPulseWithProfile(){
  fetch('/startPulse');
}

function record(){

  if(!('webkitSpeechRecognition' in window)){
    alert("Speech recognition not supported in this browser.");
    return;
  }

  let recognition = new webkitSpeechRecognition();
  recognition.lang = "en-US";
  recognition.start();

  document.getElementById("answer").innerText = "Listening... 🎤";

  recognition.onresult = function(event){
    let text = event.results[0][0].transcript;

    fetch("/ask?q=" + encodeURIComponent(text))
    .then(res => res.text())
    .then(data => {
        document.getElementById("answer").innerText = data;
    });
  };

}

function downloadReport(){
 fetch("/healthReport").then(res=>res.text()).then(data=>{
   const { jsPDF } = window.jspdf;
   let doc = new jsPDF();
   doc.text(data,10,10);
   doc.save("Nexa_Health_Report.pdf");
 });
}

/* BMI CALCULATION */

function calculateBMI(){

let weight=document.getElementById("weight").value;
let height=document.getElementById("height").value/100;

let bmi=weight/(height*height);

document.getElementById("bmi").innerText=bmi.toFixed(1);

}

/* Calling Fetch data */
setInterval(fetchData,1000);

</script>
</body>
</html>
)====";

// ---------------- Speak ----------------
void speakESP32(String text){
  if(text.length()>180) text=text.substring(0,180);
  text.replace("\n",""); text.replace(",",""); text.replace(".","");
  text.replace("?",""); text.replace("!",""); text.replace(" ","%20");
  String url="https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=en&q="+text;
  audio.setVolume(60);
  audio.connecttohost(url.c_str());
  speaking=true;
}

// ---------------- Groq AI ----------------
void askGroq(String question){
  audio.stopSong();
  delay(300);

  WiFiClientSecure client;
  client.setInsecure();

  pox.shutdown();
  pulseActive = false;

  HTTPClient http;
  http.begin(client,"https://api.groq.com/openai/v1/chat/completions");
  http.addHeader("Content-Type","application/json");
  http.addHeader("Authorization","Bearer "+GROQ_API_KEY);

  StaticJsonDocument<512> doc;
  doc["model"]="llama-3.3-70b-versatile";
  doc["max_tokens"]=60;
  doc["temperature"]=0.7;
  JsonArray messages=doc.createNestedArray("messages");
  JsonObject sys=messages.createNestedObject();
  sys["role"]="system";
  sys["content"]="You are NEXA, a friendly AI assistant created by Ankush. Reply short.";
  JsonObject msg=messages.createNestedObject();
  msg["role"]="user";
  msg["content"]=question;
  String body; serializeJson(doc,body);

  int httpCode=http.POST(body);
  if(httpCode==200){
    String response=http.getString();
    StaticJsonDocument<512> resDoc;
    deserializeJson(resDoc,response);
    String answer=resDoc["choices"][0]["message"]["content"].as<String>();
    answer += ". Please ask your next question.";
    speakESP32(answer);
    server.send(200,"text/plain",answer);
  } else {
    String error="Sorry I could not answer.";
    speakESP32(error);
    server.send(200,"text/plain",error);
  }
  http.end();
  client.stop();

  delay(200);
  pox.resume();
  pulseActive=true;
  tsLastReport=millis();
}

// ---------------- Handlers ----------------
void handleRoot(){ server.send(200,"text/html",webpage); }
void handleAsk(){ String q=server.arg("q"); askGroq(q); }

void handleData(){

  bool valid=(heartRate>=20 && heartRate<=150 && spo2>=50 && spo2<=100);

  String json="{\"hr\":"+String(heartRate,0)+
               ",\"spo2\":"+String(spo2,0)+
               ",\"temp\":\""+String(tempC,1)+
               "\",\"health\":\""+healthStatus+
               "\",\"valid\":"+(valid?"true":"false")+"}";

  server.send(200,"application/json",json);
}

void handleTemperature(){
  sensors.requestTemperatures();
  tempC = sensors.getTempCByIndex(0);
  String tempStr=(tempC == DEVICE_DISCONNECTED_C)?"Sensor not detected":String(tempC,1);
  server.send(200,"text/plain",tempStr);
}

// ---------------- Health Report ----------------
void handleHealthReport(){

    String report = "";
    report += "---- NEXA AI HEALTH REPORT ----\n";
report += "Heart Rate: " + String(heartRate,1) + " BPM\n";
report += "SpO2: " + String(spo2,1) + " %\n";
report += "Temperature: " + String(tempC,1) + " C\n";
report += "Health Status: " + healthStatus + "\n";
report += "--------------------------------";

    server.send(200,"text/plain",report);
}

void handleStartPulse() {
  pox.resume();
  pulseActive=true;
  server.send(200,"text/plain","Pulse started");
}
void handleStopPulse() {
  pox.shutdown();
  pulseActive=false;
  server.send(200,"text/plain","Pulse stopped");
}

// NEW: Health Advice
void handleHealthAdvice() {
    String advice = "Keep healthy!";

    if (heartRate >= 55 && heartRate <= 110 && spo2 >= 85 && spo2 <= 100 && tempC > 30 && tempC < 40){
        if (heartRate > 95) advice = "Your heart rate is a bit high. Eat light meals, avoid caffeine, and relax.";
        else if (heartRate < 60) advice = "Your heart rate is a bit low. Try mild exercise and stay hydrated.";
        else advice = "Your heart rate is normal. Keep a balanced diet and stay active.";

        if (spo2 < 90) advice += " Your oxygen level is slightly low. Deep breathing and fresh air are recommended.";
        if (tempC >= 37.5) advice += " You might have a fever. Stay hydrated and rest.";
        if (tempC <= 30) advice +=  " Your temperature is slightly low. Keep warm and eat nutritious food.";
    } else {
        advice = "Place your finger on the sensor and ensure temperature is being measured for advice.";
    }

    speakESP32(advice);
    server.send(200,"text/plain",advice);
}

// NEW: Speak endpoint
void handleSpeak() {
    String txt = server.arg("text");
    if(txt.length()>0) speakESP32(txt);
    server.send(200,"text/plain","ok");
}


// ---------------- Health Status ----------------
void updateHealthStatus(){

    if(heartRate >= 60 && heartRate <= 100 &&
       spo2 >= 95 &&
       tempC >= 36 && tempC <= 37.5){
        healthStatus = "Excellent";
    }
    else if(heartRate >= 50 && heartRate <= 110 &&
            spo2 >= 92){
        healthStatus = "Good";
    }
    else{
        healthStatus = "Check Health";
    }
}

// ---------------- Setup ----------------
// ---------------- Setup ----------------
void setup(){

  Serial.begin(115200);

  // Optional: improve memory stability
  heap_caps_malloc_extmem_enable(20000);

  // ---------------- WiFi ----------------
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // ---------------- I2C ----------------
  Wire.begin(21, 22);

  // 🔥 FIX: Use proper I2C speed
  Wire.setClock(100000);

  // ---------------- MAX30100 ----------------
  if (!pox.begin()) {
    Serial.println("❌ FAILED to initialize MAX30100!");
    while (1);   // Stop if sensor fails
  }

  Serial.println("MAX30100 Initialized Successfully");

  // Wait for sensor to stabilize
  delay(3000);

  pox.setOnBeatDetectedCallback(onBeatDetected);

  // 🔥 Strong LED current for better readings
  pox.setIRLedCurrent(MAX30100_LED_CURR_50MA);

  // 🔥 IMPORTANT: Start pulse immediately
  pulseActive = true;

  // ---------------- Audio (MAX98357) ----------------
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(60);

  // ---------------- Temperature Sensor ----------------
  pinMode(ONE_WIRE_BUS, INPUT_PULLUP);
  sensors.begin();

  // 🔥 Initialize health status at startup
  updateHealthStatus();

  // ---------------- Web Server Routes ----------------
  server.on("/", handleRoot);
  server.on("/ask", handleAsk);
  server.on("/data", handleData);
  server.on("/temperature", handleTemperature);
  server.on("/healthReport", handleHealthReport);
  server.on("/startPulse", handleStartPulse);
  server.on("/stopPulse", handleStopPulse);
  server.on("/healthAdvice", handleHealthAdvice);
  server.on("/speak", handleSpeak);

  server.begin();

  Serial.println("Web Server Started");

  // Welcome message
  speakESP32("Nexa is ready. You can ask a question or place your finger on the sensor.");
}

// ---------------- Loop ----------------
static bool stabilized = false;


// ---------------- Pulse Sensor Reset ----------------
void resetPulseSensor() {
    Serial.println("Performing full MAX30100 reset...");

    // Shutdown pulse oximeter
    pox.shutdown();
    delay(50);

    // Stop I2C bus completely
    Wire.end();
    delay(50);

    // Reinitialize I2C
    Wire.begin(21, 22);
    delay(50);

    // Reinitialize MAX30100
    if(!pox.begin()) {
        Serial.println("ERROR: MAX30100 failed to re-initialize!");
    } else {
        Serial.println("MAX30100 re-initialized successfully.");
    }

    pox.setOnBeatDetectedCallback(onBeatDetected);
    pox.setIRLedCurrent(MAX30100_LED_CURR_50MA);
}

// ---------------- Pulse Sensor Update ----------------
bool updatePulseSensor() {
    static unsigned long lastValid = 0;
    static float hrBuffer[5] = {0};
    static float spBuffer[5] = {0};
    static uint8_t idx = 0;

    // Update MAX30100 readings
    pox.update();

    float hr = pox.getHeartRate();
    float sp = pox.getSpO2();

    // Only consider valid readings
    if(hr >= 30 && hr <= 200 && sp >= 70 && sp <= 100){
        hrBuffer[idx] = hr;
        spBuffer[idx] = sp;
        idx = (idx + 1) % 5;
        lastValid = millis();
    }

    // If no valid reading for 2 seconds, reset sensor
    if(millis() - lastValid > 2000){
        Serial.println("Pulse sensor stuck! Performing full reset...");
        resetPulseSensor();
        lastValid = millis();
        idx = 0;
        for(int i = 0; i < 5; i++) hrBuffer[i] = 0;
        for(int i = 0; i < 5; i++) spBuffer[i] = 0;
        return false;
    }

  
    // Compute average of last 5 valid readings
    float avgHR = 0, avgSp = 0, count = 0;
    for(int i = 0; i < 5; i++){
        if(hrBuffer[i] > 0){
            avgHR += hrBuffer[i];
            avgSp += spBuffer[i];
            count++;
        }
    }
    if(count > 0){
        heartRate = avgHR / count;
        spo2 = avgSp / count;
    }

    return true;
}

  
void loop() {
    server.handleClient();
    audio.loop();

    static bool stabilized = false;
    const unsigned long REPORT_INTERVAL = 500;
    static unsigned long tsLastReport = 0;

    if(pulseActive){
        bool ok = updatePulseSensor();
        if(!ok){
            stabilized=false;
            delay(100);
            return;
        }

        if(millis()-tsLastReport>REPORT_INTERVAL){
            tsLastReport=millis();
            if(heartRate>0 && spo2>0){
                updateHealthStatus();
                Serial.print("HR=");
                Serial.print(heartRate,1);
                Serial.print(" bpm, SpO2=");
                Serial.print(spo2,1);
                Serial.println(" %");
                stabilized=true;
            } else if(!stabilized){
                Serial.println("Waiting for sensor to stabilize...");
            }
        }
    }

    if(speaking && !audio.isRunning()){
        speaking=false;
    }

    delay(1);
}
