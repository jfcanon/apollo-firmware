#include "sync_box.h"

#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include "settings.h"

#define TAG "SyncBox"

namespace {
constexpr int kTimeoutMilliseconds = 5000;
constexpr int kResponseLimitBytes = 4096;
}  // namespace

void SyncBox::LoadSettings() {
    Settings settings("syncbox", false);
    address_ = settings.GetString("address");
    token_ = settings.GetString("token");
    if (!address_.empty()) {
        ESP_LOGI(TAG, "Sync Box at %s, paired: %s", address_.c_str(),
                 token_.empty() ? "no" : "yes");
    }
}

void SyncBox::SetAddress(const std::string& address) {
    address_ = address;
    Settings settings("syncbox", true);
    settings.SetString("address", address);
}

bool SyncBox::Request(const char* method,
                      const std::string& path,
                      const std::string& body,
                      std::string& response_out,
                      std::string& error_out) {
    if (address_.empty()) {
        error_out = "no Sync Box address configured";
        return false;
    }

    const std::string url = "https://" + address_ + path;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = kTimeoutMilliseconds;
    // No CA and no CN check: the Sync Box ships a self-signed certificate whose
    // CN never matches its address. See the header for why that is acceptable.
    config.skip_cert_common_name_check = true;
    config.crt_bundle_attach = nullptr;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        error_out = "http client init failed";
        return false;
    }

    if (method[0] == 'P' && method[1] == 'U') {
        esp_http_client_set_method(client, HTTP_METHOD_PUT);
    } else if (method[0] == 'P') {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
    } else {
        esp_http_client_set_method(client, HTTP_METHOD_GET);
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (!token_.empty()) {
        const std::string authorization = "Bearer " + token_;
        esp_http_client_set_header(client, "Authorization", authorization.c_str());
    }

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, body.size());
    if (err != ESP_OK) {
        error_out = std::string("connect failed: ") + esp_err_to_name(err);
    } else {
        if (body.empty() || esp_http_client_write(client, body.data(), body.size()) >= 0) {
            esp_http_client_fetch_headers(client);
            const int status = esp_http_client_get_status_code(client);
            char buffer[513];
            int total = 0;
            int read = 0;
            while (total < kResponseLimitBytes &&
                   (read = esp_http_client_read(client, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[read] = '\0';
                response_out.append(buffer, read);
                total += read;
            }
            // 401/403 here means the token was revoked from the Hue Sync app;
            // say so plainly rather than reporting a generic failure.
            if (status == 200 || status == 204) {
                ok = true;
            } else if (status == 401 || status == 403) {
                error_out = "Sync Box rejected the token — pair again";
            } else {
                error_out = "Sync Box returned HTTP " + std::to_string(status);
            }
        } else {
            error_out = "write failed";
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

bool SyncBox::Register(std::string& error_out) {
    std::string response;
    // The Sync Box hands out a token only while its button is held down, so a
    // failure here is the normal case until the user does that.
    const std::string body = R"({"appName":"jarvis","instanceName":"apollo"})";
    if (!Request("POST", "/api/v1/registrations", body, response, error_out)) {
        if (error_out.find("HTTP 400") != std::string::npos) {
            error_out = "hold the Sync Box button for 3 seconds, then ask again";
        }
        return false;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        error_out = "could not parse the registration response";
        return false;
    }
    cJSON* access_token = cJSON_GetObjectItem(root, "accessToken");
    if (!cJSON_IsString(access_token)) {
        cJSON_Delete(root);
        error_out = "hold the Sync Box button for 3 seconds, then ask again";
        return false;
    }
    token_ = access_token->valuestring;
    Settings settings("syncbox", true);
    settings.SetString("token", token_);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Paired with the Sync Box");
    return true;
}

SyncBox::Status SyncBox::ReadStatus() {
    Status status;
    std::string response;
    if (!Request("GET", "/api/v1", "", response, status.error)) {
        return status;
    }
    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        status.error = "could not parse the Sync Box state";
        return status;
    }
    cJSON* execution = cJSON_GetObjectItem(root, "execution");
    if (execution != nullptr) {
        cJSON* sync_active = cJSON_GetObjectItem(execution, "syncActive");
        cJSON* hdmi_active = cJSON_GetObjectItem(execution, "hdmiActive");
        cJSON* mode = cJSON_GetObjectItem(execution, "mode");
        cJSON* source = cJSON_GetObjectItem(execution, "hdmiSource");
        status.syncing = cJSON_IsTrue(sync_active);
        status.powered = cJSON_IsTrue(hdmi_active) || status.syncing;
        if (cJSON_IsString(mode)) {
            status.mode = mode->valuestring;
        }
        if (cJSON_IsString(source)) {
            status.source = source->valuestring;
        }
        status.ok = true;
    } else {
        status.error = "the Sync Box state had no execution block";
    }
    cJSON_Delete(root);
    return status;
}

bool SyncBox::PutExecution(const std::string& body, std::string& error_out) {
    std::string response;
    return Request("PUT", "/api/v1/execution", body, response, error_out);
}

bool SyncBox::SetPower(bool on, std::string& error_out) {
    // hdmiActive is the box passing video through; syncActive is the lights
    // following it. Turning it "off" leaves the picture alone and stops the
    // lights, which is what a spoken "turn the sync off" means.
    return PutExecution(std::string(R"({"syncActive":)") + (on ? "true" : "false") + "}",
                        error_out);
}

bool SyncBox::SetMode(const std::string& mode, std::string& error_out) {
    if (mode != "video" && mode != "music" && mode != "game" && mode != "passthrough") {
        error_out = "mode must be video, music, game or passthrough";
        return false;
    }
    return PutExecution(R"({"mode":")" + mode + R"("})", error_out);
}

bool SyncBox::SetSource(const std::string& source, std::string& error_out) {
    if (source.rfind("input", 0) != 0) {
        error_out = "source must be input1, input2, input3 or input4";
        return false;
    }
    return PutExecution(R"({"hdmiSource":")" + source + R"("})", error_out);
}

bool SyncBox::SetBrightness(int brightness, std::string& error_out) {
    if (brightness < 0 || brightness > 200) {
        error_out = "brightness must be between 0 and 200";
        return false;
    }
    return PutExecution(R"({"brightness":)" + std::to_string(brightness) + "}", error_out);
}
