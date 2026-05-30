# ESP32-EasyWolfSSL

Easy-to-use WolfSSL wrapper for ESP32 with NetworkClientSecure (aka WiFiClientSecure) API compatibility.

## Overview

ESP32-EasyWolfSSL provides a drop-in replacement for the standard `WiFiClientSecure` class, allowing you to use WolfSSL for TLS/SSL connections on ESP32. It's fully compatible with `HTTPClient` and other libraries that expect a `WiFiClientSecure` interface.

### Why Use This Library?

- **Drop-in Replacement**: Compatible with existing code using `WiFiClientSecure`
- **WolfSSL Power**: Benefit from WolfSSL's memory-optimized and secure TLS implementation
- **Easy to Use**: Simple API that matches standard ESP32 libraries
- **Flexible**: Supports PEM and DER formats for certificates
- **Feature-Rich**: Full TLS 1.2/1.3 support with modern cipher suites

## Features

- WiFiClientSecure API compatibility
- Works seamlessly with HTTPClient
- Support for client certificates and private keys
- CA certificate validation
- PEM and DER format support

## Installation

### Arduino IDE

1. Download this repository as a ZIP file
2. In Arduino IDE, go to **Sketch → Include Library → Add .ZIP Library**
3. Select the downloaded ZIP file
4. The library will be installed in your Arduino libraries folder

## Dependencies

This library requires:
- **wolfssl by wolfSSL Inc.** Arduino library
- **esp32 by Espressif Systems** Arduino Board library

## Important WolfSSL Configuration
__(required as of version 5.8.4)__

**If you are using certificate validation** (not using `setInsecure()`), you must configure WolfSSL to allocate sufficient memory for RSA operations:

1. Locate the WolfSSL library folder in your Arduino libraries directory:
   - Windows: `Documents\Arduino\libraries\wolfssl\src\`
   - macOS: `~/Documents/Arduino/libraries/wolfssl/src/`
   - Linux: `~/Arduino/libraries/wolfssl/src/`

2. Open the file `user_settings.h` in the `wolfssl/src/` folder

3. Search for "#elif defined(ESP32)" and add the following within this #elif block:
   ```c
   #define FP_MAX_BITS 8192
   ```
Example:
   ```c
   #elif defined(ESP32)  || \
      defined(WIFI_101) || defined(WIFI_NINA) || defined(WIFIESPAT) || \
      defined(ETHERNET_H) || defined(ARDUINO_TEENSY41) || \
      defined(ARDUINO_SAMD_MKR1000)

      #define USE_CERT_BUFFERS_2048
      #define FP_MAX_BITS 8192
      // code continues below...
   ```
4. Save the file and recompile your sketch

**Without this configuration**, certificate verification will fail under certain circumstances.

**Note**: You only need to do this once. The setting will apply to all ESP32 sketches using WolfSSL.

## Quick Start

### Basic HTTPS Request (with HTTPClient)

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <WolfSSLClient.h>

const char* ssid = "your_SSID";
const char* password = "your_PASSWORD";

void setup() {
    Serial.begin(115200);
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    
    // Create secure client
    WolfSSLClient client;
    client.setInsecure();  // Skip certificate verification for testing
    
    // Make HTTPS request
    HTTPClient https;
    if (https.begin(client, "https://www.howsmyssl.com/a/check")) {
        int httpCode = https.GET();
        if (httpCode > 0) {
            String payload = https.getString();
            Serial.println(payload);
        }
        https.end();
    }
}

void loop() {}
```

### Direct TLS Connection

```cpp
#include <WiFi.h>
#include <WolfSSLClient.h>

void setup() {
    Serial.begin(115200);
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    
    WolfSSLClient client;
    client.setInsecure();  // Skip certificate verification for testing
    
    if (client.connect("example.com", 443)) {
        client.write((const uint8_t*)"GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", 38);
        
        while (client.available()) {
            char c = client.read();
            Serial.print(c);
        }
        
        client.stop();
    }
}

void loop() {}
```

### Secure HTTPS with Certificate Validation

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <WolfSSLClient.h>

// Your target host's issuing root CA certificate in PEM format (Base 64)
const char* root_ca = "-----BEGIN CERTIFICATE-----\n"
                      "MIIF...\n"  // Your certificate here
                      "-----END CERTIFICATE-----\n";

void setup() {
    Serial.begin(115200);
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    
    WolfSSLClient client;
    client.setCACert(root_ca);  // Use specified root CA certificate to verify presented host cert
    
    HTTPClient https;
    if (https.begin(client, "https://secure-site.com/api")) {
        int httpCode = https.GET();
        // ... handle response
        https.end();
    }
}

void loop() {}
```

**Important**: Certificate validation requires configuring WolfSSL's `user_settings.h`. See the [WolfSSL Configuration](#important-wolfssl-configuration) section above.

## API Reference

### Initialization

#### `static bool initialize()`
Initialize the WolfSSL library. This is called automatically in the constructor, so explicit calls are optional. You can call it explicitly in `setup()` if you want to check for initialization errors.

```cpp
if (!WolfSSLClient::initialize()) {
    Serial.println("WolfSSL initialization failed");
}
```

#### `static void setDebug(bool enable)`
Enable or disable debug output.

```cpp
WolfSSLClient::setDebug(true);  // Enable WolfSSL debug output
```

### Connection Methods

#### `int connect(const char *host, uint16_t port)`
Connect to a TLS server.

```cpp
if (client.connect("example.com", 443)) {
    // Connected!
}
```

#### `void stop()`
Close the connection and clean up resources.

```cpp
client.stop();
```

#### `uint8_t connected()`
Check if still connected.

```cpp
if (client.connected()) {
    // Still connected
}
```

### Certificate Management

#### `void setCACert(const char* rootCA)`
Set root CA certificate (PEM format) for server verification.

```cpp
client.setCACert(root_ca_pem);
```

#### `void setCertificate(const char* client_cert)`
Set client certificate (PEM format) for mutual authentication.

```cpp
client.setCertificate(client_cert_pem);
```

#### `void setPrivateKey(const char* private_key)`
Set private key (PEM format) for mutual authentication.

```cpp
client.setPrivateKey(private_key_pem);
```

#### `void setCACertDer(const uint8_t* ca_cert, size_t ca_cert_len)`
Set root CA certificate in DER format.

```cpp
client.setCACertDer(ca_cert_der, ca_cert_len);
```

#### `void setInsecure()`
Skips certificate verification. Useful for non-production testing.

```cpp
client.setInsecure();  // Not secure. Use only for development.
```

### Data Transfer

#### `size_t write(const uint8_t *buf, size_t size)`
Write data to the connection.

```cpp
const char* data = "Hello!";
client.write((const uint8_t*)data, strlen(data));
```

#### `int read(uint8_t *buf, size_t size)`
Read data from the connection.

```cpp
uint8_t buffer[1024];
int len = client.read(buffer, sizeof(buffer));
```

#### `int available()`
Get number of bytes available to read.

```cpp
while (client.available()) {
    char c = client.read();
    Serial.print(c);
}
```

### Information Methods

#### `const char* getProtocolVersion()`
Get the TLS protocol version being used.

```cpp
Serial.println(client.getProtocolVersion());  // e.g., "TLSv1.2"
```

#### `const char* getCipherSuite()`
Get the cipher suite being used.

```cpp
Serial.println(client.getCipherSuite());  // e.g., "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256"
```

## Examples

The library includes several examples:

1. **SimpleHTTPS** - Basic HTTPS GET request using HTTPClient
2. **SecureHTTPS** - HTTPS with proper certificate verification (requires WolfSSL library configuration - see above)

## Migration from WiFiClientSecure

This library is designed as a drop-in replacement. Simply change:

```cpp
// Before
#include <WiFiClientSecure.h>
WiFiClientSecure client;

// After (be sure to REMOVE #include <WiFiClientSecure.h>)
#include <WolfSSLClient.h>
WiFiClientSecure client;  // You are now using WolfSSL
```

Most `WiFiClientSecure` code will work without changes

## Troubleshooting

### Certificate Verification Errors (RSA Memory Issue)

If you see errors like:
```
RSA_FUNCTION MP_EXPTMOD_E: memory/config problem
ASN sig error, confirm failure
```

**Solution**: You need to configure WolfSSL's memory settings. See the [WolfSSL Configuration](#important-wolfssl-configuration) section above for detailed instructions to add `#define FP_MAX_BITS 8192` to `user_settings.h`.

### Connection Fails

1. Enable debug mode: `WolfSSLClient::setDebug(true);`
2. Check certificate format (PEM should include headers/footers)
3. Verify time synchronization (see SecureHTTPS example)
3. Verify the server's certificate is valid and not expired
4. Try `setInsecure()` to test if it's a certificate issue

### Memory Issues

WolfSSL can use significant RAM. If you encounter memory issues:
- Use `NO_SESSION_CACHE` or `SMALL_SESSION_CACHE` in WolfSSL's user_settings.h configuration file
- Reduce buffer sizes in your application
- Use DER format certificates (smaller than PEM)

### Handshake Timeout

- Check network connectivity
- Verify firewall settings
- Ensure the server supports compatible cipher suites
- Increase timeout values if on slow network

## Configuration

WolfSSL configuration is done through `user_settings.h` in the WolfSSL library.  Instructions for adding these settings are above under [Dependencies](#Dependencies).

Common settings:

```c
// Necessary for proper operation if validating certificates
#define FP_MAX_BITS 8192

// Memory optimization
#define SMALL_SESSION_CACHE

// Disable insecure DES3
#define NO_DES3

// Disable insecure TLS versions (TLS 1.0 and 1.1)
#define NO_OLD_TLS
```

## Security Notes

1. **Never use `setInsecure()` in production** - This disables certificate verification, breaking security
2. **Always validate certificates** using `setCACert()` with a proper root CA
3. **Use strong cipher suites** - For better security, NO_DES3 and NO_OLD_TLS should be configured in user_settings.h
4. **Protect private keys** - Private keys should be maintained separate from your main codebase

## Unimplemented Features

The following NetworkClientSecure/WiFiClientSecure features are not implemented:

### Basic Features

1. **`peek()` method** - Always returns `-1` to indicate the operation is not supported. Use `available()` and `read()` instead

### Certificate and Key Loading

2. **Stream-based certificate loading** - `loadCACert()`, `loadCertificate()`, and `loadPrivateKey()` methods that load certificates from Stream objects are not implemented. Use `setCACert()`, `setCertificate()`, and `setPrivateKey()` with string buffers instead

3. **CA certificate bundle** - `setCACertBundle()` is not implemented. Use `setCACert()` with individual root CA certificates

4. **Certificate fingerprint pinning** - `verify()` method exists but does not perform actual fingerprint verification. Use `setCACert()` for standard certificate chain validation instead

### Advanced TLS Features

5. **Pre-Shared Keys (PSK)** - `setPreSharedKey()` is not implemented. PSK cipher suites are not currently supported

6. **ALPN (Application-Layer Protocol Negotiation)** - `setAlpnProtocols()` is not implemented

7. **Handshake timeout configuration** - `setHandshakeTimeout()` is not implemented. Default timeout behavior is used

8. **STARTTLS support** - `setPlainStart()`, `stillInPlainStart()`, and `startTLS()` are not implemented. Only direct TLS connections are supported

### Certificate Inspection

9. **Peer certificate access** - `getPeerCertificate()` and `getFingerprintSHA256()` are not implemented

**Note**: These limitations do not affect typical HTTPS/TLS use cases with HTTPClient and standard certificate validation.

## License

This library follows the same license as WolfSSL. Check the WolfSSL license for details.

## Credits

- Based on WolfSSL by wolfSSL Inc.
- Espressif WiFiClientSecure API

This library is a wrapper around wolfSSL (https://www.wolfssl.com/) and follows the same license terms as wolfSSL.

For commercial licensing options, please contact wolfSSL Inc.
