/*
 * WolfSSLClient.h - ESP32-EasyWolfSSL Library
 * 
 * WiFiClientSecure API-compatible wrapper for WolfSSL
 * Provides drop-in replacement for standard ESP32 WiFiClientSecure
 * 
 * Compatible with HTTPClient and other libraries expecting WiFiClientSecure
 */

#ifndef WOLFSSLCLIENT_H
#define WOLFSSLCLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>

// WolfSSL includes
#include <wolfssl.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/ssl.h>
#include <wolfssl/certs_test.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

class WolfSSLClient : public WiFiClient {
public:
    WolfSSLClient();
    ~WolfSSLClient();

    // Connection methods
    int connect(IPAddress ip, uint16_t port) override;
    int connect(const char *host, uint16_t port) override;
    int connect(IPAddress ip, uint16_t port, int32_t timeout);
    int connect(const char *host, uint16_t port, int32_t timeout);
    
    // Data transfer
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *buf, size_t size) override;
    int available() override;
    int read() override;
    int read(uint8_t *buf, size_t size) override;
    int peek() override;
    
    // Connection management
    void stop() override;
    uint8_t connected() override;
    operator bool() override;

    // Certificate management (WiFiClientSecure compatible)
    void setCACert(const char* rootCA);
    void setCertificate(const char* client_cert);
    void setPrivateKey(const char* private_key);
    void setCACertDer(const uint8_t* ca_cert, size_t ca_cert_len);
    void setCertificateDer(const uint8_t* client_cert, size_t client_cert_len);
    void setPrivateKeyDer(const uint8_t* private_key, size_t private_key_len);
    
    // Additional security options
    void setInsecure(); // Skip certificate verification
    void setPreSharedKey(const char *psk, const char *identity);
    
    // Get TLS information
    const char* getProtocolVersion();
    const char* getCipherSuite();
    bool verify(const char* fingerprint, const char* domain_name);

    // Static initialization (call once in setup())
    static bool initialize();
    static void setDebug(bool enable);

private:
    WiFiClient _basic_client;
    WOLFSSL_CTX* _ctx;
    WOLFSSL* _ssl;
    bool _connected;
    bool _ssl_established;
    bool _insecure;
    
    // Certificate buffers
    const char* _ca_cert;
    const char* _client_cert;
    const char* _private_key;
    const uint8_t* _ca_cert_der;
    size_t _ca_cert_der_len;
    const uint8_t* _client_cert_der;
    size_t _client_cert_der_len;
    const uint8_t* _private_key_der;
    size_t _private_key_der_len;
    
    // Connection info cache to persist after disconnect
    String _cached_protocol;
    String _cached_cipher;
    
    // Timeout configuration
    int32_t _timeout_ms;
    
    // Static context flag
    static bool _wolfssl_initialized;
    static bool _debug_enabled;
    
    // Internal methods
    bool _createSSLContext();
    bool _setupCertificates();
    bool _performSSLHandshake(int32_t timeout_ms);
    void _cleanup();
    
    // WolfSSL I/O callbacks
    static int _send_cb(WOLFSSL* ssl, char* buf, int sz, void* ctx);
    static int _recv_cb(WOLFSSL* ssl, char* buf, int sz, void* ctx);
};

// Alias WiFiClientSecure for compatibility with existing code
typedef WolfSSLClient WiFiClientSecure;

#endif // WOLFSSLCLIENT_H
