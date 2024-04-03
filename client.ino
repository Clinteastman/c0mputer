#define DEBUG_WEBSOCKETS

#include <driver/i2s.h>
#include <M5Cardputer.h>
#include <Arduino.h>  //not needed in the arduino ide
#include <AsyncTCP.h> //https://github.com/me-no-dev/AsyncTCP using the latest dev version from @me-no-dev
#include <DNSServer.h>
#include <ESPAsyncWebServer.h> //https://github.com/me-no-dev/ESPAsyncWebServer using the latest dev version from @me-no-dev
#include <esp_wifi.h>          //Used for mpdu_rx_disable android workaround
#include <ArduinoHttpClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <Preferences.h>

Preferences preferences;

String server_domain = "";
int server_port = 10001;

int lineHeightDefault = 12;
int lineHeight = lineHeightDefault;
int yPos = 0; // Keeps track of the current vertical position for text display

// ----------------------- START OF WIFI CAPTIVE PORTAL -------------------

// Pre reading on the fundamentals of captive portals https://textslashplain.com/2022/06/24/captive-portals/

const char *ssid = "c0mputer"; // FYI The SSID can't have a space in it.
// const char * password = "12345678"; //Atleast 8 chars
const char *password = NULL; // no password

#define MAX_CLIENTS 4  // ESP32 supports up to 10 but I have not tested it yet
#define WIFI_CHANNEL 6 // 2.4ghz channel 6 https://en.wikipedia.org/wiki/List_of_WLAN_channels#2.4_GHz_(802.11b/g/n/ax)
#define DNS_INTERVAL 30 // Define the DNS interval in milliseconds between processing DNS requests

const IPAddress localIP(4, 3, 2, 1);          // the IP address the web server, Samsung requires the IP to be in public space
const IPAddress gatewayIP(4, 3, 2, 1);        // IP address of the network should be the same as the local IP for captive portals
const IPAddress subnetMask(255, 255, 255, 0); // no need to change: https://avinetworks.com/glossary/subnet-mask/
const String localIPURL = "http://4.3.2.1";   // a string version of the local IP with http, used for redirecting clients to your webpage

// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30 * 1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;

String generateHTMLWithSSIDs()
{
 String html = "<!DOCTYPE html><html><head><title>WiFi Setup</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<style> *{box-sizing: border-box;} body {background-color: #fff; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; flex-direction: column; font-family: Helvetica, sans-serif;} "
    "h1 {color: black; text-align: center; margin: 40px;}"
    "form {display: flex; flex-direction: column; align-items: center;} "
    "input[type='text'], input[type='password'], select {margin-bottom: 10px; font-size: 16px; padding: 8px;} "
    "input[type='submit'] {background-color: black; color: white; border: none; padding: 10px 20px; cursor: pointer; font-size: 16px; margin-top: 28px;} "
    "input[type='submit']:hover {background-color: #333; }#otherSSID {display: none;}</style>"
    "<script>function checkSSID(value) {"
    "var otherSSID = document.getElementById('otherSSID');"
    "if(value === 'OTHER') { otherSSID.style.display = 'block'; } else { otherSSID.style.display = 'none'; }}"
    "</script></head>"
    "<body><h1>01 Light</h1><form action='/submit' method='post'>"
    "<div><div><label for='ssid'>WiFi Network Name:</label><br><br>"
    "<select id='ssid' name='ssid' onchange='checkSSID(this.value);'>";

    int n = WiFi.scanComplete();
    for (int i = 0; i < n; ++i)
    {
        html += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
    }

    html += "<option value='OTHER'>Other</option></select><br><br><input type='text' id='otherSSID' name='otherSSID' placeholder='Enter WiFi Name'><br></div><div><label for='password'>Password:</label><br><br>"
            "<input type='password' id='password' name='password' ><br></div></div>"
            "<input type='submit' value='Connect'/></form></body></html>";

    return html;
}

const char post_connected_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>c0mputer Setup</title>
  <style>
  
  * {
      box-sizing: border-box;
    }
    
    body {
      background-color: #fff;
      margin: 0;
      padding: 0;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      flex-direction: column;
      font-family: Helvetica, sans-serif;
    }

    h1 {
      color: black;
      text-align: center;
      margin-bottom: 40px;
    }
    form {
      display: flex;
      flex-direction: column;
    }
    input[type="text"]{
      width: 100%;
      font-size: 16px;
      padding: 8px;
    }
    input[type="submit"] {
      background-color: black;
      color: white;
      border: none;
      padding: 10px 20px;
      cursor: pointer;
      font-size: 16px;
      margin-top: 20px;
      width: 100%;
    }
    input[type="submit"]:hover {
      background-color: #333;
    }
    


    #error_message {
    color: red;
    font-weight: bold;
    text-align: center; 
    width: 100%;
    margin-top: 20px; 
    max-width: 300px;
}
  </style>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
</head>
<body>
  <h1>01OS Setup</h1>
  <form action="/submit_01os" method="post">
    <div class="contain">
      <label for="server_address">Server Address:</label><br><br>
      <input type="text" id="server_address" name="server_address"><br><br>
    </div>

   

    <input type="submit" value="Connect"/>
     <p id="error_message"></p>
  </form>
</body>
</html>
)=====";

String successHtml = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>c0mputer Setup</title>
  <style>
    body {
      background-color: #fff;
      margin: 0;
      padding: 0;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      flex-direction: column;
      font-family: Arial, sans-serif;
    }
    h2 {
      color: black;
      text-align: center;
      margin-bottom: 0px;

  </style>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
</head>
<body>
  <h2>Connected to 01OS!</h1>
  <p>You can now close this window</p>
</body>
</html>
)=====";

DNSServer dnsServer;
AsyncWebServer server(80);

void dualOutput(bool addNewLine, const char *format, ...) {
    char buf[256]; // Buffer to hold the formatted string
    va_list args;

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args); // Format the string using the arguments
    va_end(args);

    Serial.print(buf); // Print to Serial

    // Before printing to the display, check and handle vertical scrolling
    if (yPos + lineHeight > M5Cardputer.Display.height()) {
        M5Cardputer.Display.scroll(0, -lineHeight); // Scroll the display content up by one line height
        yPos -= lineHeight; // Adjust yPos accordingly to "free up" space at the bottom
    }

    // Print to the M5Cardputer display at the current yPos
    M5Cardputer.Display.setCursor(0, yPos);
    M5Cardputer.Display.print(buf);

    // If required, add a new line
    if (addNewLine) {
        Serial.println();
        M5Cardputer.Display.println(); // This adds a new line on the display too
        yPos += lineHeight; // Move yPos to the next start line position
    } else {
        // If we're not adding a new line, simply account for the text just printed
        yPos += lineHeight; // Adjust this if a single line might not match your lineHeight
    }

    // Optional: if you want to clear the area where the new text will go, uncomment below
    M5Cardputer.Display.fillRect(0, yPos, M5Cardputer.Display.width(), lineHeight, BLACK);

    // Note: Depending on how new lines are handled internally by your display library,
    // you might need to adjust yPos differently when addNewLine is true.
}

// Overloaded version for Arduino String objects
void dualOutput(bool addNewLine, const String &message) {
  dualOutput(addNewLine, "%s", message.c_str()); // Forward to the original as a formatted string
}

void dualNewLine() {
  Serial.println();
  M5.Lcd.println();
}

void setUpDNSServer(DNSServer &dnsServer, const IPAddress &localIP)
{
    // Set the TTL for DNS response and start the DNS server
    dnsServer.setTTL(3600);
    dnsServer.start(53, "*", localIP);
}

void startSoftAccessPoint(const char *ssid, const char *password, const IPAddress &localIP, const IPAddress &gatewayIP)
{
    // Set the WiFi mode to access point and station
    WiFi.mode(WIFI_MODE_AP);

    // Define the subnet mask for the WiFi network
    const IPAddress subnetMask(255, 255, 255, 0);

    // Configure the soft access point with a specific IP and subnet mask
    WiFi.softAPConfig(localIP, gatewayIP, subnetMask);

    // Start the soft access point with the given ssid, password, channel, max number of clients
    WiFi.softAP(ssid, password, WIFI_CHANNEL, 0, MAX_CLIENTS);

    // Disable AMPDU RX on the ESP32 WiFi to fix a bug on Android
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
    my_config.ampdu_rx_enable = false;
    esp_wifi_init(&my_config);
    esp_wifi_start();
    vTaskDelay(100 / portTICK_PERIOD_MS); // Add a small delay
}

void connectToWifi(String ssid, String password)
{
    dualOutput(true, "Init Wi-Fi...");
    WiFi.begin(ssid.c_str(), password.c_str());
    // WiFi.begin(ssid, password);
    Serial.println("Done");
    dualOutput(true, "Done");

    // Wait for connection to establish
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(1000);
        dualOutput(false, ".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        dualOutput(true, "Connected to Wi-Fi");

        // Store credentials on successful connection
        preferences.begin("wifi", false); // Open Preferences with my-app namespace. RW-mode is false by default.
        preferences.putString("ssid", ssid); // Put your SSID.
        preferences.putString("password", password); // Put your PASSWORD.
        preferences.end(); // Close the Preferences.
    }
    else
    {
        dualOutput(true, "Failed to connect to Wi-Fi. Check credentials.");
    }
}

bool connectTo01OS(String server_address)
{
    int err = 0;
    int port = 80;

    String domain;
    String portStr;

    // Remove http and https, as it causes errors in HttpClient, the library relies on adding the host header itself
    if (server_address.startsWith("http://")) {
        server_address.remove(0, 7);

    } else if (server_address.startsWith("https://")) {
        server_address.remove(0, 8);

    }

    // Remove trailing slash, causes issues
    if (server_address.endsWith("/")) {
        server_address.remove(server_address.length() - 1);
    }

    int colonIndex = server_address.indexOf(':');
    if (colonIndex != -1) {
        domain = server_address.substring(0, colonIndex);
        portStr = server_address.substring(colonIndex + 1);
    } else {
        domain = server_address;
        portStr = ""; 
    }

    WiFiClient c;


    //If there is a port, set it
    if (portStr.length() > 0) {
        port = portStr.toInt();
    }

    HttpClient http(c, domain.c_str(), port); 
    dualOutput(true, "Connecting to 01OS at " + domain + ":" + port + "/ping");

    if (domain.indexOf("ngrok") != -1) {
        http.sendHeader("ngrok-skip-browser-warning", "80");
    }

    err = http.get("/ping");
    bool connectionSuccess = false;

    if (err == 0)
    {
        dualOutput(true, "Started the ping request");

        err = http.responseStatusCode();
        if (err >= 0)
        {
            dualOutput(false, "Got status code: ");
            dualOutput(true, "%d", err);

            if (err == 200)
            {
                server_domain = domain;
                server_port = port;
                connectionSuccess = true;
            }

            err = http.skipResponseHeaders();
            if (err >= 0)
            {
                int bodyLen = http.contentLength();
                dualOutput(false, "Content length is: ");
                dualOutput(true, "%d", bodyLen);
                dualNewLine();
                dualOutput(true, "Body:");

                // Now we've got to the body, so we can print it out
                unsigned long timeoutStart = millis();
                char c;
                // Whilst we haven't timed out & haven't reached the end of the body
                while ((http.connected() || http.available()) &&
                    ((millis() - timeoutStart) < kNetworkTimeout))
                {
                    if (http.available())
                    {
                        c = http.read();
                        // Print out this character

                        M5Cardputer.Display.print(c);
                        M5Cardputer.Display.print("");

                        bodyLen--;
                        // We read something, reset the timeout counter
                        timeoutStart = millis();
                    }
                    else
                    {
                        // We haven't got any data, so let's pause to allow some to
                        // arrive
                        delay(kNetworkDelay);
                    }
                }
            }
            else
            {
                dualOutput(false, "Failed to skip response headers: ");
                dualOutput(true, "%d", err);
            }
        }
        else
        {
            dualOutput(false, "Getting response failed: ");
            dualOutput(true, "%d", err);
        }
    }
    else
    {
        dualOutput(false, "Connection failed: ");
        dualOutput(true, "%d", err);
    }
  return connectionSuccess;
}

void setUpWebserver(AsyncWebServer &server, const IPAddress &localIP)
{
    //======================== Webserver ========================
    // WARNING IOS (and maybe macos) WILL NOT POP UP IF IT CONTAINS THE WORD "Success" https://www.esp8266.com/viewtopic.php?f=34&t=4398
    // SAFARI (IOS) IS STUPID, G-ZIPPED FILES CAN'T END IN .GZ https://github.com/homieiot/homie-esp8266/issues/476 this is fixed by the webserver serve static function.
    // SAFARI (IOS) there is a 128KB limit to the size of the HTML. The HTML can reference external resources/images that bring the total over 128KB
    // SAFARI (IOS) popup browser has some severe limitations (javascript disabled, cookies disabled)

    // Required
    server.on("/connecttest.txt", [](AsyncWebServerRequest *request)
              { request->redirect("http://logout.net"); }); // windows 11 captive portal workaround
    server.on("/wpad.dat", [](AsyncWebServerRequest *request)
              { request->send(404); }); // Honestly don't understand what this is but a 404 stops win 10 keep calling this repeatedly and panicking the esp32 :)

    // Background responses: Probably not all are Required, but some are. Others might speed things up?
    // A Tier (commonly used by modern systems)
    server.on("/generate_204", [](AsyncWebServerRequest *request)
              { request->redirect(localIPURL); }); // android captive portal redirect
    server.on("/redirect", [](AsyncWebServerRequest *request)
              { request->redirect(localIPURL); }); // microsoft redirect
    server.on("/hotspot-detect.html", [](AsyncWebServerRequest *request)
              { request->redirect(localIPURL); }); // apple call home
    server.on("/canonical.html", [](AsyncWebServerRequest *request)
              { request->redirect(localIPURL); }); // firefox captive portal call home
    server.on("/success.txt", [](AsyncWebServerRequest *request)
              { request->send(200); }); // firefox captive portal call home
    server.on("/ncsi.txt", [](AsyncWebServerRequest *request)
              { request->redirect(localIPURL); }); // windows call home

    // B Tier (uncommon)
    //  server.on("/chrome-variations/seed",[](AsyncWebServerRequest *request){request->send(200);}); //chrome captive portal call home
    //  server.on("/service/update2/json",[](AsyncWebServerRequest *request){request->send(200);}); //firefox?
    //  server.on("/chat",[](AsyncWebServerRequest *request){request->send(404);}); //No stop asking Whatsapp, there is no internet connection
    //  server.on("/startpage",[](AsyncWebServerRequest *request){request->redirect(localIPURL);});

    // return 404 to webpage icon
    server.on("/favicon.ico", [](AsyncWebServerRequest *request)
              { request->send(404); }); // webpage icon

    // Serve Basic HTML Page
    server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request)
              {
	String htmlContent = "";
    Serial.printf("Wifi scan complete: %d . WIFI_SCAN_RUNNING: %d", WiFi.scanComplete(), WIFI_SCAN_RUNNING);
    if(WiFi.scanComplete() > 0) {
      // Scan complete, process results
      dualOutput(true, "Done scanning wifi");
      htmlContent = generateHTMLWithSSIDs();
      // WiFi.scanNetworks(true); // Start a new scan in async mode
    }
		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", htmlContent);
		response->addHeader("Cache-Control", "public,max-age=31536000");  // save this file to cache for 1 year (unless you refresh)
		request->send(response);
		dualOutput(true, "Served HTML Page"); });

    // the catch all
    server.onNotFound([](AsyncWebServerRequest *request)
                      {
		request->redirect(localIPURL);
		dualOutput(false, "onnotfound ");
		dualOutput(false, request->host());	// This gives some insight into whatever was being requested on the serial monitor
		dualOutput(false, " ");
		dualOutput(false, request->url());
		dualOutput(false, " sent redirect to " + localIPURL + "\n"); });

    server.on("/submit", HTTP_POST, [](AsyncWebServerRequest *request)
              {
    String ssid;
    String password;
    
    // Check if SSID parameter exists and assign it
    if(request->hasParam("ssid", true)) {
        ssid = request->getParam("ssid", true)->value();
        // If "OTHER" is selected, use the value from "otherSSID"
        if(ssid == "OTHER" && request->hasParam("otherSSID", true)) {
            ssid = request->getParam("otherSSID", true)->value();
            dualOutput(true, "OTHER SSID SELECTED: " + ssid);
        }
    }
    
    // Check if Password parameter exists and assign it
    if(request->hasParam("password", true)) {
        password = request->getParam("password", true)->value();
    }
    // Serial.println(ssid);
    // Serial.println(password);

    // Attempt to connect to the Wi-Fi network with these credentials
    if(request->hasParam("password", true) && request->hasParam("ssid", true)) {
      connectToWifi(ssid, password);
    }

    // Redirect user or send a response back
    if (WiFi.status() == WL_CONNECTED) {
      String htmlContent = post_connected_html;
      AsyncWebServerResponse *response = request->beginResponse(200, "text/html", htmlContent);
      response->addHeader("Cache-Control", "public,max-age=31536000");  // save this file to cache for 1 year (unless you refresh)
      request->send(response);
      dualOutput(true, "Served Post connection HTML Page"); 
    } else {
      request->send(200, "text/plain", "Failed to connect to " + ssid);
    } });

    server.on("/submit_01os", HTTP_POST, [](AsyncWebServerRequest *request)
              {
    String server_address;
    
    // Check if SSID parameter exists and assign it
    if(request->hasParam("server_address", true)) {
        server_address = request->getParam("server_address", true)->value();
    }

    // Attempt to connect to the Wi-Fi network with these credentials
    bool connectedToServer = connectTo01OS(server_address);

    // Redirect user or send a response back
    String connectionMessage;

    if (connectedToServer)
    {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", successHtml);
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");  // Prevent caching of this page
        request->send(response);     
        dualOutput(true, " ");
        dualOutput(true, "Connected to 01 websocket!");
        dualOutput(true, " ");
        dualOutput(true, "Served success HTML Page");
    }
    else
    {
      // If connection fails, serve the error page instead of sending plain text
        String htmlContent = String(post_connected_html); // Load your HTML template
        // Inject the error message
        htmlContent.replace("<p id=\"error_message\"></p>", "<p id=\"error_message\" style=\"color: red;\">Error connecting, please try again.</p>");
        
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", htmlContent);
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");  // Prevent caching of this page
        request->send(response);
        dualOutput(true, "Served Post connection HTML Page with error message");
    }
    });
}
void tryReconnectWiFi() {
    dualOutput(true, "Checking for stored WiFi credentials...");
    preferences.begin("wifi", true); // Open Preferences with my-app namespace in ReadOnly mode
    String ssid = preferences.getString("ssid", ""); // Get stored SSID, if any
    String password = preferences.getString("password", ""); // Get stored password, if any
    preferences.end(); // Close the Preferences

    if (ssid != "") { // Check if we have stored credentials
        dualOutput(true, "Trying to connect to WiFi with stored credentials.");
        WiFi.begin(ssid.c_str(), password.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            dualOutput(false, ".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            dualOutput(true, "Connected to Wi-Fi using stored credentials.");
            tryReconnectToServer();
            return;
        } else {
            dualOutput(true, "Failed to connect to Wi-Fi. Starting captive portal.");
        }
    } else {
        dualOutput(true, "No stored WiFi credentials. Starting captive portal.");
    }
}
void tryReconnectToServer() {
    preferences.begin("network", true); // Open Preferences with the "network" namespace in ReadOnly mode
    String serverURL = preferences.getString("server_url", ""); // Get stored server URL, if any
    preferences.end(); // Close the Preferences

    if (!serverURL.isEmpty()) {
        dualOutput(true, "Trying to reconnect to server with stored URL: " + serverURL);
        // Attempt to connect to the server using the stored URL
        if (connectTo01OS(serverURL)) {
            dualOutput(true, "Reconnected to server using stored URL.");
        } else {
            dualOutput(true, "Failed to reconnect to server. Proceeding with normal startup.");
            // Proceed with your normal startup routine, possibly involving user input to get a new URL
        }
    } else {
        dualOutput(true, "No stored server URL. Proceeding with normal startup.");
        // Normal startup routine
    }
}
// ----------------------- END OF WIFI CAPTIVE PORTAL -------------------

// ----------------------- START OF PLAYBACK -------------------

#define CONFIG_I2S_BCK_PIN 19
#define CONFIG_I2S_LRCK_PIN 33
#define CONFIG_I2S_DATA_PIN 22
#define CONFIG_I2S_DATA_IN_PIN 23
#define SPEAKER_I2S_NUMBER I2S_NUM_0
#define MODE_MIC 0
#define MODE_SPK 1
#define DATA_SIZE 1024

#define MAX_DATA_LEN (1024 * 9)

uint8_t microphonedata0[1024 * 10];
uint8_t speakerdata0[1024 * 1];
int speaker_offset;
int data_offset;

bool recording = false;
WebSocketsClient webSocket;

class ButtonChecker
{
public:
    void loop()
    {
        lastTickState = thisTickState;
        thisTickState = M5.BtnA.isPressed() != 0;
    }

    bool justPressed()
    {
        return thisTickState && !lastTickState;
    }

    bool justReleased()
    {
        return !thisTickState && lastTickState;
    }

private:
    bool lastTickState = false;
    bool thisTickState = false;
};

ButtonChecker button = ButtonChecker();

void InitI2SSpeakerOrMic(int mode)
{
    dualOutput(false, "InitI2sSpeakerOrMic %d\n", mode);
    esp_err_t err = ESP_OK;

    i2s_driver_uninstall(SPEAKER_I2S_NUMBER);
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER),
        .sample_rate = 16000,
        .bits_per_sample =
            I2S_BITS_PER_SAMPLE_16BIT, // is fixed at 12bit, stereo, MSB
        .channel_format = I2S_CHANNEL_FMT_ALL_RIGHT,
#if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 1, 0)
        .communication_format =
            I2S_COMM_FORMAT_STAND_I2S, // Set the format of the communication.
#else                                 
        .communication_format = I2S_COMM_FORMAT_I2S,
#endif
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 60,
    };
    if (mode == MODE_MIC)
    {
        i2s_config.mode =
            (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    }
    else
    {
        i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
        i2s_config.use_apll = false;
        i2s_config.tx_desc_auto_clear = true;
    }

    err += i2s_driver_install(SPEAKER_I2S_NUMBER, &i2s_config, 0, NULL);
    i2s_pin_config_t tx_pin_config;

#if (ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 3, 0))
    tx_pin_config.mck_io_num = I2S_PIN_NO_CHANGE;
#endif
    tx_pin_config.bck_io_num = CONFIG_I2S_BCK_PIN;
    tx_pin_config.ws_io_num = CONFIG_I2S_LRCK_PIN;
    tx_pin_config.data_out_num = CONFIG_I2S_DATA_PIN;
    tx_pin_config.data_in_num = CONFIG_I2S_DATA_IN_PIN;
    err += i2s_set_pin(SPEAKER_I2S_NUMBER, &tx_pin_config);
    err += i2s_set_clk(SPEAKER_I2S_NUMBER, 16000, I2S_BITS_PER_SAMPLE_16BIT,
                    I2S_CHANNEL_MONO);
}

void speaker_play(uint8_t *payload, uint32_t len)
{
    dualOutput(false, "received %lu bytes", len);
    size_t bytes_written;
    InitI2SSpeakerOrMic(MODE_SPK);
    i2s_write(SPEAKER_I2S_NUMBER, payload, len,
            &bytes_written, portMAX_DELAY);
}

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    dualOutput(false, "[WSc] Disconnected!\n");
    dualOutput(true, "[Debug] WebSocket disconnected");
    break;
  case WStype_CONNECTED:
    dualOutput(false, "[WSc] Connected to url: %s\n", payload);
    dualOutput(true, "[Debug] WebSocket connected to URL: %s\n", payload);
    break;
  case WStype_TEXT:
  {
    dualOutput(false, "[WSc] get text: %s\n", payload);
    dualOutput(true, "[Debug] Received text: %s\n", payload);
    std::string str(payload, payload + length);
    bool isAudio = str.find("\"audio\"") != std::string::npos;
    if (isAudio && str.find("\"start\"") != std::string::npos)
    {
      dualOutput(true, "start playback");
      dualOutput(true, "[Debug] Starting audio playback");
      speaker_offset = 0;
      InitI2SSpeakerOrMic(MODE_SPK);
    }
    else if (isAudio && str.find("\"end\"") != std::string::npos)
    {
      dualOutput(true, "end playback");
      dualOutput(true, "[Debug] Ending audio playback");
    }
    break;
  }
  case WStype_BIN:
    dualOutput(false, "[WSc] get binary length: %u\n", length);
    dualOutput(true, "[Debug] Received binary data of length: %u\n", length);
    memcpy(speakerdata0 + speaker_offset, payload, length);
    speaker_offset += length;
    size_t bytes_written;
    i2s_write(SPEAKER_I2S_NUMBER, speakerdata0, speaker_offset, &bytes_written, portMAX_DELAY);
    speaker_offset = 0;
    
        // send data to server
        // webSocket.sendBIN(payload, length);
    break;
  case WStype_ERROR:
  case WStype_FRAGMENT_TEXT_START:
  case WStype_FRAGMENT_BIN_START:
  case WStype_FRAGMENT:
  case WStype_FRAGMENT_FIN:
    dualOutput(true, "[Debug] Received unhandled WebSocket event type");
    break;
  }
}

void websocket_setup(String server_domain, int port)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        dualOutput(true, "Not connected to WiFi. Abandoning setup websocket");
        return;
    }
    dualOutput(true, "connected to WiFi");
    webSocket.begin(server_domain, port, "/");
    webSocket.onEvent(webSocketEvent);
    // webSocket.setAuthorization("user", "Password");
    webSocket.setReconnectInterval(5000);
    dualOutput(true, "Websocket setup fininshed");
}

void flush_microphone()
{
    dualOutput(false, "[microphone] flushing and sending %d bytes of data\n", data_offset);
    if (data_offset == 0)
        return;
    webSocket.sendBIN(microphonedata0, data_offset);
    data_offset = 0;
}

void audio_recording_task(void *arg) {
    while (1) {
        if (recording) {
            dualOutput(false, "Reading chunk at %d...\n", data_offset);
            size_t bytes_read;
            i2s_read(
                SPEAKER_I2S_NUMBER,
                (char *)(microphonedata0 + data_offset),
                DATA_SIZE, &bytes_read, (100 / portTICK_RATE_MS));
            data_offset += bytes_read;
            dualOutput(false, "Read %d bytes in chunk.\n", bytes_read);

            // Only send here
            if (data_offset > MAX_DATA_LEN)
            {
                flush_microphone();
                delay(10);
            }
        }
        else {
            delay(100);    // Wait for recording event
        }
    }
}

// ----------------------- END OF PLAYBACK -------------------

bool hasSetupWebsocket = false;
bool isServerURLStored() {
    preferences.begin("network", true); // Open Preferences with the "network" namespace in ReadOnly mode
    String serverURL = preferences.getString("server_url", ""); // Get stored server URL, if any
    preferences.end(); // Close the Preferences
    return !serverURL.isEmpty();
}
// M5Cardputer setup
M5Canvas canvas(&M5Cardputer.Display);
String commandBuffer              = "> ";
int cursorY                       = 0;
unsigned long lastKeyPressMillis  = 0;
const unsigned long debounceDelay = 200;  // Adjust debounce delay as needed

void setup()
{
    // Set the transmit buffer size for the Serial object and start it with a baud rate of 115200.
    Serial.setTxBufferSize(1024);
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    int textsize = M5Cardputer.Display.height() / 80;
    lineHeight = textsize * lineHeightDefault;
    if (textsize == 0) {
        textsize = 1;
        lineHeight = lineHeightDefault;
    }
    dualOutput(true, "textsize = %d", textsize);
    dualOutput(true, "lineHeight = %d", lineHeight);
    M5Cardputer.Display.setTextSize(textsize);  // Set text size
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.fillScreen(BLACK);

    // Wait for the Serial object to become available.
    while (!Serial)
        ;

    WiFi.mode(WIFI_AP_STA);

    // Print a welcome message to the Serial port.
    dualOutput(true, "\n\nCaptive Test, V0.5.0 compiled " __DATE__ " " __TIME__ " by CD_FER"); //__DATE__ is provided by the platformio ide
    dualOutput(false, "%s-%d\n\r", ESP.getChipModel(), ESP.getChipRevision());

    dualOutput(true, "starting accesspoint");
        // If WiFi reconnect fails, start the soft access point for the captive portal.
    if (WiFi.status() != WL_CONNECTED) {
        startSoftAccessPoint(ssid, password, localIP, gatewayIP);
        dualOutput(true, "setting up DNS server...");
        setUpDNSServer(dnsServer, localIP);
        dualOutput(true, "scanNetworks setting true...");
        WiFi.scanNetworks(true); // Start scanning for networks in preparation for the captive portal.
        dualOutput(true, "setting up web server...");
        setUpWebserver(server, localIP); // Set up the web server for the captive portal.
        dualOutput(true, "starting web server...");
    }

    server.begin(); // Begin the web server.

    dualNewLine();
    dualOutput(false, "Startup Time:"); // should be somewhere between 270-350 for Generic ESP32 (D0WDQ6 chip, can have a higher startup time on first boot)
    dualOutput(true, "%lu ms", millis());
    dualNewLine();

    /* Create task for I2S */
    xTaskCreate(audio_recording_task, "AUDIO", 4096, NULL, 4, NULL); // Create a task for audio recording.
}

void loop()
{
    // Don't use delay here, should use elapsed time
    uint32_t last_dns_ms = 0;
    if ((millis() - last_dns_ms) > DNS_INTERVAL) {
        last_dns_ms = millis();            // seems to help with stability, if you are doing other things in the loop this may not be needed
        dnsServer.processNextRequest();    // I call this atleast every 10ms in my other projects (can be higher but I haven't tested it for stability)
    }         

    // Check WiFi connection status
    if (WiFi.status() == WL_CONNECTED && !hasSetupWebsocket)
    {
        if (server_domain != "")
        {
            dualOutput(true, "Setting up websocket to 01OS " + server_domain + ":" + server_port);
            websocket_setup(server_domain, server_port);
            dualOutput(true, "setting MODE_SPK...");
            InitI2SSpeakerOrMic(MODE_SPK);

            hasSetupWebsocket = true;

            dualOutput(true, "Websocket connection flow completed");
        }
    }

    if (WiFi.status() == WL_CONNECTED && hasSetupWebsocket)
    {
        button.loop();
        if (button.justPressed())
        {
            dualOutput(true, "Recording...");
            webSocket.sendTXT("{\"role\": \"user\", \"type\": \"audio\", \"format\": \"bytes.raw\", \"start\": true}");
            InitI2SSpeakerOrMic(MODE_MIC);
            recording = true;
            data_offset = 0;
            dualOutput(true, "Recording ready.");
        }
        else if (button.justReleased())
        {
            dualOutput(true, "Stopped recording.");
            webSocket.sendTXT("{\"role\": \"user\", \"type\": \"audio\", \"format\": \"bytes.raw\", \"end\": true}");
            flush_microphone();
            recording = false;
            data_offset = 0;
        }

        M5.update();
        webSocket.loop();
    }
}