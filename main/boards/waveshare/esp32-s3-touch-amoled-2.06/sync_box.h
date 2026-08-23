#ifndef SYNC_BOX_H_
#define SYNC_BOX_H_

#include <string>

// Philips Hue Play HDMI Sync Box, spoken to over the LAN.
//
// The Sync Box is invisible to the Hue cloud: it is not a bridge device, and
// /resource/entertainment only ever lists the lamps as renderers with the
// bridge as proxy. Its own API on port 443 is the only way to switch inputs or
// set a sync mode — which means something on the LAN has to hold the token.
// That something is this device: it is already on the LAN, and it is neither
// the Mac nor the deliberately isolated VM.
//
// The certificate is self-signed and its CN does not match the IP, so the
// requests here run without verification. That is safe only because the peer is
// a fixed private address on the home LAN and the token grants nothing beyond
// the Sync Box itself.
class SyncBox {
public:
    struct Status {
        bool ok = false;
        bool powered = false;
        bool syncing = false;
        std::string mode;    // video | music | game | passthrough
        std::string source;  // input1..input4
        std::string error;
    };

    // Address and token live in NVS ("syncbox"), so pairing survives a reboot.
    void LoadSettings();
    bool IsPaired() const { return !address_.empty() && !token_.empty(); }
    std::string address() const { return address_; }

    void SetAddress(const std::string& address);

    // Presses-and-pairs: the Sync Box only issues a token while its button is
    // held, so this is expected to fail until the user does that.
    bool Register(std::string& error_out);

    Status ReadStatus();
    bool SetPower(bool on, std::string& error_out);
    // mode: video | music | game | passthrough
    bool SetMode(const std::string& mode, std::string& error_out);
    // source: input1 | input2 | input3 | input4
    bool SetSource(const std::string& source, std::string& error_out);
    bool SetBrightness(int brightness, std::string& error_out);

private:
    bool Request(const char* method,
                 const std::string& path,
                 const std::string& body,
                 std::string& response_out,
                 std::string& error_out);
    bool PutExecution(const std::string& body, std::string& error_out);

    std::string address_;
    std::string token_;
};

#endif  // SYNC_BOX_H_
