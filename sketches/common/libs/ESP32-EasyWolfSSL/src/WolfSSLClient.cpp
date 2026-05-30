/*
 * WolfSSLClient.cpp - ESP32-EasyWolfSSL Library
 * 
 * Implementation of WiFiClientSecure-compatible wrapper for WolfSSL
 */

#include "WolfSSLClient.h"

// Static member initialization
bool WolfSSLClient::_wolfssl_initialized = false;
bool WolfSSLClient::_debug_enabled = false;

/**
 * Constructor
 */
WolfSSLClient::WolfSSLClient() {
    _ctx = nullptr;
    _ssl = nullptr;
    _connected = false;
    _ssl_established = false;
    _insecure = false;
    _ca_cert = nullptr;
    _client_cert = nullptr;
    _private_key = nullptr;
    _ca_cert_der = nullptr;
    _ca_cert_der_len = 0;
    _client_cert_der = nullptr;
    _client_cert_der_len = 0;
    _private_key_der = nullptr;
    _private_key_der_len = 0;
    _cached_protocol = "";
    _cached_cipher = "";
    _timeout_ms = 30000; // 30 second default timeout
    
    // Initialize WolfSSL globally if not already done
    initialize();
}

/**
 * Destructor
 */
WolfSSLClient::~WolfSSLClient() {
    _cleanup();
}

/**
 * Initialize WolfSSL library (static, call once)
 */
bool WolfSSLClient::initialize() {
    if (!_wolfssl_initialized) {
        int ret = wolfSSL_Init();
        if (ret == WOLFSSL_SUCCESS) {
            _wolfssl_initialized = true;
            if (_debug_enabled) {
                Serial.println("WolfSSL initialized successfully");
            }
            return true;
        } else {
            Serial.println("ERROR: WolfSSL initialization failed");
            return false;
        }
    }
    return true;
}

/**
 * Enable/disable debug output
 */
void WolfSSLClient::setDebug(bool enable) {
    _debug_enabled = enable;
#ifdef DEBUG_WOLFSSL
    if (enable) {
        wolfSSL_Debugging_ON();
    } else {
        wolfSSL_Debugging_OFF();
    }
#endif
}

/**
 * Connect to server with TLS
 */
int WolfSSLClient::connect(IPAddress ip, uint16_t port) {
    return connect(ip.toString().c_str(), port);
}

/**
 * Connect to server with TLS (main implementation)
 */
int WolfSSLClient::connect(const char *host, uint16_t port) {
    if (_debug_enabled) {
        Serial.printf("WolfSSLClient: Connecting to %s:%d\n", host, port);
    }
    
    // Clean up any existing connection
    if (_connected || _ssl_established) {
        stop();
    }
    
    // Establish basic TCP connection first
    if (!_basic_client.connect(host, port)) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: TCP connection failed");
        }
        return 0;
    }
    
    // Give the TCP connection a moment
    delay(50);
    
    // Verify connection is actually established
    if (!_basic_client.connected()) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: TCP connection lost immediately");
        }
        _basic_client.stop();
        return 0;
    }
    
    _connected = true;
    
    // Create SSL context and configure it
    if (!_createSSLContext()) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: Failed to create SSL context");
        }
        _basic_client.stop();
        _connected = false;
        return 0;
    }
    
    // Setup certificates if provided
    if (!_setupCertificates()) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: Failed to setup certificates");
        }
        _cleanup();
        _basic_client.stop();
        _connected = false;
        return 0;
    }
    
    // Create SSL object
    _ssl = wolfSSL_new(_ctx);
    if (_ssl == nullptr) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: Failed to create SSL object");
        }
        _cleanup();
        _basic_client.stop();
        _connected = false;
        return 0;
    }
    
    // Set I/O callbacks on the SSL object (not context) for proper operation
    wolfSSL_SSLSetIORecv(_ssl, _recv_cb);
    wolfSSL_SSLSetIOSend(_ssl, _send_cb);
    wolfSSL_SetIOReadCtx(_ssl, (void*)this);
    wolfSSL_SetIOWriteCtx(_ssl, (void*)this);
    
    // Perform SSL handshake
    if (!_performSSLHandshake(_timeout_ms)) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: SSL handshake failed");
        }
        _cleanup();
        _basic_client.stop();
        _connected = false;
        return 0;
    }
    
    _ssl_established = true;
    
    // Cache connection info for later retrieval
    _cached_protocol = wolfSSL_get_version(_ssl);
    _cached_cipher = wolfSSL_get_cipher(_ssl);
    
    if (_debug_enabled) {
        Serial.println("WolfSSLClient: SSL connection established");
        Serial.printf("  Protocol: %s\n", _cached_protocol.c_str());
        Serial.printf("  Cipher: %s\n", _cached_cipher.c_str());
    }
    
    return 1;
}

/**
 * Connect with timeout
 */
int WolfSSLClient::connect(IPAddress ip, uint16_t port, int32_t timeout) {
    return connect(ip.toString().c_str(), port, timeout);
}

/**
 * Connect with timeout
 */
int WolfSSLClient::connect(const char *host, uint16_t port, int32_t timeout) {
    if (_debug_enabled) {
        Serial.printf("WolfSSLClient: Connecting to %s:%d (timeout: %dms)\n", host, port, timeout);
    }
    
    // Clean up any existing connection
    if (_connected || _ssl_established) {
        stop();
    }
    
    // Store timeout for this connection
    int32_t saved_timeout = _timeout_ms;
    _timeout_ms = timeout;
    
    // Set timeout on underlying WiFiClient (in seconds)
    _basic_client.setTimeout((timeout + 999) / 1000); // Round up
    
    // Establish basic TCP connection first
    if (!_basic_client.connect(host, port)) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: TCP connection failed");
        }
        _timeout_ms = saved_timeout;
        return 0;
    }
    
    // Give the TCP connection a moment
    delay(50);
    
    // Verify connection is actually established
    if (!_basic_client.connected()) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: TCP connection lost immediately");
        }
        _basic_client.stop();
        _timeout_ms = saved_timeout;
        return 0;
    }
    
    _connected = true;
    
    // Create SSL context and configure it
    if (!_createSSLContext()) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: Failed to create SSL context");
        }
        _basic_client.stop();
        _connected = false;
        _timeout_ms = saved_timeout;
        return 0;
    }
    
    // Setup certificates if provided
    if (!_setupCertificates()) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: Failed to setup certificates");
        }
        _cleanup();
        _basic_client.stop();
        _connected = false;
        _timeout_ms = saved_timeout;
        return 0;
    }
    
    // Create SSL object
    _ssl = wolfSSL_new(_ctx);
    if (_ssl == nullptr) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: Failed to create SSL object");
        }
        _cleanup();
        _basic_client.stop();
        _connected = false;
        _timeout_ms = saved_timeout;
        return 0;
    }
    
    // Set I/O callbacks on the SSL object (not context) for proper operation
    wolfSSL_SSLSetIORecv(_ssl, _recv_cb);
    wolfSSL_SSLSetIOSend(_ssl, _send_cb);
    wolfSSL_SetIOReadCtx(_ssl, (void*)this);
    wolfSSL_SetIOWriteCtx(_ssl, (void*)this);

#ifdef HAVE_SNI
    /* PATCH 2 (voir PATCHES.md): envoyer le SNI, exige par les serveurs derriere passerelle (ex: PRIM) */
    if (host != nullptr) {
        wolfSSL_UseSNI(_ssl, WOLFSSL_SNI_HOST_NAME, host, (word16)strlen(host));
    }
#endif

    // Perform SSL handshake with timeout
    if (!_performSSLHandshake(timeout)) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: SSL handshake failed");
        }
        _cleanup();
        _basic_client.stop();
        _connected = false;
        _timeout_ms = saved_timeout;
        return 0;
    }
    
    _ssl_established = true;
    
    // Cache connection info for later retrieval
    _cached_protocol = wolfSSL_get_version(_ssl);
    _cached_cipher = wolfSSL_get_cipher(_ssl);
    
    if (_debug_enabled) {
        Serial.println("WolfSSLClient: SSL connection established");
        Serial.printf("  Protocol: %s\n", _cached_protocol.c_str());
        Serial.printf("  Cipher: %s\n", _cached_cipher.c_str());
    }
    
    return 1;
}

/**
 * Write single byte
 */
size_t WolfSSLClient::write(uint8_t data) {
    return write(&data, 1);
}

/**
 * Write buffer
 */
size_t WolfSSLClient::write(const uint8_t *buf, size_t size) {
    if (!_ssl_established || !_connected) {
        return 0;
    }
    
    int ret = wolfSSL_write(_ssl, buf, size);
    if (ret < 0) {
        int err = wolfSSL_get_error(_ssl, ret);
        if (_debug_enabled) {
            char errBuf[80];
            wolfSSL_ERR_error_string(err, errBuf);
            Serial.printf("WolfSSLClient: Write error: %s\n", errBuf);
        }
        return 0;
    }
    
    return ret;
}

/**
 * Number of bytes available to read
 */
int WolfSSLClient::available() {
    if (!_ssl_established) {
        return 0;
    }
    
    // Check if there's buffered SSL data first (important for proper SSL operation)
    int pending = wolfSSL_pending(_ssl);
    if (pending > 0) {
        return pending;
    }
    
    // Check underlying socket for new data
    // Note: We check this even if _connected is false, because the server
    // might have closed the connection after sending data (HTTP/1.0 behavior)
    return _basic_client.available();
}

/**
 * Read single byte
 */
int WolfSSLClient::read() {
    uint8_t data;
    int ret = read(&data, 1);
    if (ret > 0) {
        return data;
    }
    return -1;
}

/**
 * Read buffer
 */
int WolfSSLClient::read(uint8_t *buf, size_t size) {
    // Allow reading even if connection is closed, as there might be buffered data
    // This handles the case where the server closes the connection after sending the response
    if (!_ssl_established) {
        return -1;
    }
    
    int ret = wolfSSL_read(_ssl, buf, size);
    if (ret < 0) {
        int err = wolfSSL_get_error(_ssl, ret);
        if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
            return 0; // Would block, no data available
        }
        if (_debug_enabled) {
            char errBuf[80];
            wolfSSL_ERR_error_string(err, errBuf);
            Serial.printf("WolfSSLClient: Read error: %s (code: %d)\n", errBuf, err);
        }
        return -1;
    }
    
    if (_debug_enabled && ret > 0) {
        Serial.printf("WolfSSLClient: Read %d bytes\n", ret);
    }
    
    return ret;
}

/**
 * Peek at next byte without consuming it
 * Note: Not supported for TLS connections - always returns -1
 */
int WolfSSLClient::peek() {
    // peek() cannot be properly implemented for TLS without buffering
    // Returning -1 to indicate this operation is not supported
    return -1;
}

/**
 * Stop connection and cleanup
 */
void WolfSSLClient::stop() {
    if (_ssl_established && _ssl != nullptr) {
        // Graceful shutdown
        int ret = wolfSSL_shutdown(_ssl);
        int retry = 100; // Max 100ms
        while (ret == WOLFSSL_SHUTDOWN_NOT_DONE && retry > 0) {
            delay(1);
            ret = wolfSSL_shutdown(_ssl);
            retry--;
        }
    }
    
    _cleanup();
    _basic_client.stop();
    _connected = false;
    _ssl_established = false;
}

/**
 * Check if connected
 */
uint8_t WolfSSLClient::connected() {
    if (!_connected) {
        return 0;
    }
    
    // Check underlying connection
    if (!_basic_client.connected()) {
        _connected = false;
        _ssl_established = false;
        return 0;
    }
    
    return 1;
}

/**
 * Bool operator
 */
WolfSSLClient::operator bool() {
    return connected();
}

/**
 * Set CA certificate (PEM format)
 */
void WolfSSLClient::setCACert(const char* rootCA) {
    _ca_cert = rootCA;
}

/**
 * Set client certificate (PEM format)
 */
void WolfSSLClient::setCertificate(const char* client_cert) {
    _client_cert = client_cert;
}

/**
 * Set private key (PEM format)
 */
void WolfSSLClient::setPrivateKey(const char* private_key) {
    _private_key = private_key;
}

/**
 * Set CA certificate (DER format)
 */
void WolfSSLClient::setCACertDer(const uint8_t* ca_cert, size_t ca_cert_len) {
    _ca_cert_der = ca_cert;
    _ca_cert_der_len = ca_cert_len;
}

/**
 * Set client certificate (DER format)
 */
void WolfSSLClient::setCertificateDer(const uint8_t* client_cert, size_t client_cert_len) {
    _client_cert_der = client_cert;
    _client_cert_der_len = client_cert_len;
}

/**
 * Set private key (DER format)
 */
void WolfSSLClient::setPrivateKeyDer(const uint8_t* private_key, size_t private_key_len) {
    _private_key_der = private_key;
    _private_key_der_len = private_key_len;
}

/**
 * Skip certificate verification (INSECURE - for testing only!)
 */
void WolfSSLClient::setInsecure() {
    _insecure = true;
    if (_debug_enabled) {
        Serial.println("WolfSSLClient: WARNING - Certificate verification disabled!");
    }
}

/**
 * Set pre-shared key (PSK)
 * WARNING: Not implemented - PSK is not supported
 */
void WolfSSLClient::setPreSharedKey(const char *psk, const char *identity) {
    // PSK support not implemented
    Serial.println("WARNING: WolfSSLClient::setPreSharedKey() is not implemented. PSK cipher suites are not supported.");
}

/**
 * Get TLS protocol version
 */
const char* WolfSSLClient::getProtocolVersion() {
    if (_ssl_established && _ssl != nullptr) {
        return wolfSSL_get_version(_ssl);
    }
    // Return cached value if connection was established previously
    if (_cached_protocol.length() > 0) {
        return _cached_protocol.c_str();
    }
    return "Not connected";
}

/**
 * Get cipher suite
 */
const char* WolfSSLClient::getCipherSuite() {
    if (_ssl_established && _ssl != nullptr) {
        return wolfSSL_get_cipher(_ssl);
    }
    // Return cached value if connection was established previously
    if (_cached_cipher.length() > 0) {
        return _cached_cipher.c_str();
    }
    return "Not connected";
}

/**
 * Verify certificate fingerprint
 */
bool WolfSSLClient::verify(const char* fingerprint, const char* domain_name) {
    if (_debug_enabled) {
        Serial.println("WolfSSLClient: Fingerprint verification not yet implemented");
    }
    return true;
}

/**
 * Create SSL context
 */
bool WolfSSLClient::_createSSLContext() {
    if (_ctx != nullptr) {
        wolfSSL_CTX_free(_ctx);
        _ctx = nullptr;
    }
    
    WOLFSSL_METHOD* method = wolfSSLv23_client_method();  /* PATCH 1 (voir PATCHES.md): methode flexible -> negocie jusqu'a TLS 1.3 (origine: wolfTLSv1_2_client_method = verrou 1.2) */
    if (method == nullptr) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: Failed to get TLS method");
        }
        return false;
    }
    
    _ctx = wolfSSL_CTX_new(method);
    if (_ctx == nullptr) {
        if (_debug_enabled) {
            Serial.println("WolfSSLClient: Failed to create context");
        }
        return false;
    }
    
    // Set verification mode
    if (_insecure) {
        wolfSSL_CTX_set_verify(_ctx, WOLFSSL_VERIFY_NONE, nullptr);
    } else {
        wolfSSL_CTX_set_verify(_ctx, WOLFSSL_VERIFY_PEER, nullptr);
    }
    
    return true;
}

/**
 * Setup certificates
 */
bool WolfSSLClient::_setupCertificates() {
    int ret;
    
    // Load CA certificate (PEM)
    if (_ca_cert != nullptr) {
        ret = wolfSSL_CTX_load_verify_buffer(_ctx, 
                                              (const unsigned char*)_ca_cert,
                                              strlen(_ca_cert),
                                              WOLFSSL_FILETYPE_PEM);
        if (ret != WOLFSSL_SUCCESS) {
            if (_debug_enabled) {
                Serial.printf("WolfSSLClient: Failed to load CA cert (PEM): %d\n", ret);
            }
            return false;
        }
    }
    
    // Load CA certificate (DER)
    if (_ca_cert_der != nullptr && _ca_cert_der_len > 0) {
        ret = wolfSSL_CTX_load_verify_buffer(_ctx,
                                              _ca_cert_der,
                                              _ca_cert_der_len,
                                              WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            if (_debug_enabled) {
                Serial.printf("WolfSSLClient: Failed to load CA cert (DER): %d\n", ret);
            }
            return false;
        }
    }
    
    // Load client certificate (PEM)
    if (_client_cert != nullptr) {
        ret = wolfSSL_CTX_use_certificate_buffer(_ctx,
                                                  (const unsigned char*)_client_cert,
                                                  strlen(_client_cert),
                                                  WOLFSSL_FILETYPE_PEM);
        if (ret != WOLFSSL_SUCCESS) {
            if (_debug_enabled) {
                Serial.printf("WolfSSLClient: Failed to load client cert (PEM): %d\n", ret);
            }
            return false;
        }
    }
    
    // Load client certificate (DER)
    if (_client_cert_der != nullptr && _client_cert_der_len > 0) {
        ret = wolfSSL_CTX_use_certificate_buffer(_ctx,
                                                  _client_cert_der,
                                                  _client_cert_der_len,
                                                  WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            if (_debug_enabled) {
                Serial.printf("WolfSSLClient: Failed to load client cert (DER): %d\n", ret);
            }
            return false;
        }
    }
    
    // Load private key (PEM)
    if (_private_key != nullptr) {
        ret = wolfSSL_CTX_use_PrivateKey_buffer(_ctx,
                                                 (const unsigned char*)_private_key,
                                                 strlen(_private_key),
                                                 WOLFSSL_FILETYPE_PEM);
        if (ret != WOLFSSL_SUCCESS) {
            if (_debug_enabled) {
                Serial.printf("WolfSSLClient: Failed to load private key (PEM): %d\n", ret);
            }
            return false;
        }
    }
    
    // Load private key (DER)
    if (_private_key_der != nullptr && _private_key_der_len > 0) {
        ret = wolfSSL_CTX_use_PrivateKey_buffer(_ctx,
                                                 _private_key_der,
                                                 _private_key_der_len,
                                                 WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            if (_debug_enabled) {
                Serial.printf("WolfSSLClient: Failed to load private key (DER): %d\n", ret);
            }
            return false;
        }
    }
    
    return true;
}

/**
 * Perform SSL handshake with timeout
 */
bool WolfSSLClient::_performSSLHandshake(int32_t timeout_ms) {
    int ret;
    int err;
    unsigned long start_time = millis();
    unsigned long deadline = start_time + timeout_ms;
    
    do {
        ret = wolfSSL_connect(_ssl);
        
        if (ret == WOLFSSL_SUCCESS) {
            return true;
        }
        
        err = wolfSSL_get_error(_ssl, ret);
        
        if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
            // Check if timeout exceeded
            if (millis() >= deadline) {
                if (_debug_enabled) {
                    Serial.printf("WolfSSLClient: Handshake timeout after %ldms\n", 
                                millis() - start_time);
                }
                return false;
            }
            // Async operation, keep trying
            delay(10);
            continue;
        }
        
        // Real error occurred
        if (_debug_enabled) {
            char errBuf[80];
            wolfSSL_ERR_error_string(err, errBuf);
            Serial.printf("WolfSSLClient: Handshake failed: %s (code: %d)\n", errBuf, err);
        }
        return false;
        
    } while (millis() < deadline);
    
    if (_debug_enabled) {
        Serial.println("WolfSSLClient: Handshake timeout");
    }
    return false;
}

/**
 * Cleanup SSL resources
 */
void WolfSSLClient::_cleanup() {
    if (_ssl != nullptr) {
        wolfSSL_free(_ssl);
        _ssl = nullptr;
    }
    
    if (_ctx != nullptr) {
        wolfSSL_CTX_free(_ctx);
        _ctx = nullptr;
    }
}

/**
 * WolfSSL send callback
 */
int WolfSSLClient::_send_cb(WOLFSSL* ssl, char* buf, int sz, void* ctx) {
    WolfSSLClient* client = (WolfSSLClient*)ctx;
    if (client == nullptr || !client->_connected) {
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    }
    
    // Check if the underlying socket is still connected
    if (!client->_basic_client.connected()) {
        if (client->_debug_enabled) {
            Serial.println("_send_cb: Socket disconnected");
        }
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    }
    
    // WiFiClient::write returns size_t (unsigned), 0 indicates failure
    size_t sent = client->_basic_client.write((const uint8_t*)buf, sz);
    
    if (client->_debug_enabled) {
        if (sent == (size_t)sz) {
            Serial.printf("_send_cb: Sent %d bytes\n", sz);
        } else {
            Serial.printf("_send_cb: Requested %d, sent %d bytes\n", sz, sent);
        }
    }
    
    if (sent == 0) {
        // Write failed - check if connection is still alive
        if (client->_basic_client.connected()) {
            // Connection alive but can't write - retry later
            if (client->_debug_enabled) {
                Serial.println("_send_cb: Write returned 0, requesting retry");
            }
            return WOLFSSL_CBIO_ERR_WANT_WRITE;
        } else {
            // Connection is dead
            if (client->_debug_enabled) {
                Serial.println("_send_cb: Connection closed during write");
            }
            return WOLFSSL_CBIO_ERR_CONN_CLOSE;
        }
    }
    
    return (int)sent;
}

/**
 * WolfSSL receive callback
 */
int WolfSSLClient::_recv_cb(WOLFSSL* ssl, char* buf, int sz, void* ctx) {
    WolfSSLClient* client = (WolfSSLClient*)ctx;
    if (client == nullptr) {
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    }
    
    // First, try to read any available data without waiting
    // Do this even if connection appears closed, as there might be buffered data
    int received = 0;
    while (client->_basic_client.available() && received < sz) {
        int c = client->_basic_client.read();
        if (c < 0) {
            break;
        }
        buf[received++] = (char)c;
    }
    
    // If we got some data, return it immediately
    if (received > 0) {
        if (client->_debug_enabled) {
            Serial.printf("_recv_cb: Received %d bytes\n", received);
        }
        return received;
    }
    
    // No immediate data available. Check if we should wait for more.
    // If the connection is closed and no data is available, we're done
    if (!client->_basic_client.connected()) {
        if (client->_debug_enabled) {
            Serial.println("_recv_cb: Connection closed, no buffered data");
        }
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    }
    
    // Connection is still open, wait for data to arrive
    // Use the configured timeout from the client instance
    int timeout = client->_timeout_ms;
    while (!client->_basic_client.available() && timeout > 0) {
        if (!client->_basic_client.connected()) {
            // Connection closed while waiting - check once more for buffered data
            if (client->_basic_client.available()) {
                break; // Data arrived just as connection closed
            }
            if (client->_debug_enabled) {
                Serial.println("_recv_cb: Connection closed while waiting");
            }
            return WOLFSSL_CBIO_ERR_CONN_CLOSE;
        }
        delay(1);
        timeout--;
    }
    
    // Try reading again after wait
    while (client->_basic_client.available() && received < sz) {
        int c = client->_basic_client.read();
        if (c < 0) {
            break;
        }
        buf[received++] = (char)c;
    }
    
    if (received > 0) {
        if (client->_debug_enabled) {
            Serial.printf("_recv_cb: Received %d bytes after wait\n", received);
        }
        return received;
    }
    
    // No data available, signal WolfSSL to retry
    return WOLFSSL_CBIO_ERR_WANT_READ;
}
