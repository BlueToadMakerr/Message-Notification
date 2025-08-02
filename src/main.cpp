#include <Geode/Geode.hpp>

// Binding includes for your layers
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/binding/LevelSelectLayer.hpp>
#include <Geode/binding/GJGarageLayer.hpp>
#include <Geode/binding/GJShopLayer.hpp>
#include <Geode/binding/CreatorLayer.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/SecretRewardsLayer.hpp>
#include <Geode/binding/SecretLayer.hpp>
#include <Geode/binding/SecretLayer2.hpp>
#include <Geode/binding/SecretLayer3.hpp>
#include <Geode/binding/SecretLayer4.hpp>
#include <Geode/binding/LevelAreaInnerLayer.hpp>
#include <Geode/binding/SecretLayer5.hpp>
#include <Geode/binding/GauntletSelectLayer.hpp>
#include <Geode/binding/GauntletLayer.hpp>

#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Log.hpp>

#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>
#include <vector>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;

class MessageCheckerBase : public cocos2d::Ref {
protected:
    std::chrono::steady_clock::time_point nextCheck = std::chrono::steady_clock::now();
    bool bootChecked = false;

public:
    static std::vector<std::string> split(const std::string& str, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }

    static std::string cleanMessage(const std::string& msg) {
        auto parts = split(msg, ':');
        std::string cleaned;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i] == "7" || parts[i] == "8") {
                ++i; // skip tag and value
                continue;
            }
            if (!cleaned.empty()) cleaned += ":";
            cleaned += parts[i];
        }
        return cleaned;
    }

    void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(),
            "",
            "GJ_messageIcon_001.png",
            false
        );
        log::info("Notification shown: {}", title);
    }

    void checkMessages() {
        log::info("Checking messages");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("Not logged in, skipping check");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);
        log::info("Sending HTTP request with data: {}", postData);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.length());
        req->setTag("MessageCheck");

        // Use lambda callback, which is accepted by CCHttpRequest::setResponseCallback
        req->setResponseCallback([this](cocos2d::extension::CCHttpClient* client, cocos2d::extension::CCHttpResponse* resp) {
            this->onMessageResponse(client, resp);
        });

        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onMessageResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        if (!resp || !resp->isSucceed()) {
            log::info("HTTP request failed or null response");
            return;
        }

        std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("Response data: {}", raw);

        if (raw.empty() || raw == "-1") {
            log::info("No new messages");
            return;
        }

        auto currentRaw = split(raw, '|');
        std::vector<std::string> currentClean;
        for (auto& msg : currentRaw) {
            auto cleaned = cleanMessage(msg);
            currentClean.push_back(cleaned);
            log::info("Cleaned message: {}", cleaned);
        }

        auto savedRaw = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto oldMessages = split(savedRaw, '|');

        std::vector<std::string> newMessages;
        for (auto& clean : currentClean) {
            if (std::find(oldMessages.begin(), oldMessages.end(), clean) == oldMessages.end()) {
                newMessages.push_back(clean);
            }
        }

        if (!newMessages.empty()) {
            log::info("Found {} new messages", newMessages.size());
            if (newMessages.size() == 1) {
                auto parts = split(newMessages[0], ':');
                if (parts.size() > 9) {
                    std::string sender = parts[1];
                    std::string subjectBase64 = parts[9];
                    auto decoded = geode::utils::base64::decode(subjectBase64);
                    std::string subject = decoded.isOk()
                        ? std::string(decoded.unwrap().begin(), decoded.unwrap().end())
                        : "<Invalid base64>";

                    std::string notif = fmt::format("New Message!\nSent by: {}\n{}", sender, subject);
                    showNotification(notif);
                } else {
                    showNotification("New Message!");
                }
            } else {
                showNotification(fmt::format("{} New Messages!", newMessages.size()));
            }
        } else {
            log::info("No new messages detected");
        }

        std::string save;
        for (size_t i = 0; i < currentClean.size(); i++) {
            if (i != 0) save += "|";
            save += currentClean[i];
        }
        Mod::get()->setSavedValue("last-messages", save);
        log::info("Saved current messages");
    }

    void onScheduledTick(float) {
        int interval = 300;
        try {
            interval = Mod::get()->getSettingValue<int>("check-interval");
            interval = std::clamp(interval, 60, 600);
        } catch (...) {
            log::info("Using default interval 300");
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= nextCheck) {
            checkMessages();
            nextCheck = now + std::chrono::seconds(interval);
        }
    }
};

static MessageCheckerBase g_messageChecker;

#define BINDING_LAYER_CHECKER(LayerName) \
class MessageChecker_##LayerName : public LayerName { \
protected: \
    bool init() override { \
        if (!LayerName::init()) return false; \
        if (!g_messageChecker.bootChecked) { \
            g_messageChecker.bootChecked = true; \
            this->schedule([&](float dt) { g_messageChecker.onScheduledTick(dt); }, 1.0f, "MessageCheckerScheduler"); \
            g_messageChecker.onScheduledTick(0); \
        } else { \
            this->schedule([&](float dt) { g_messageChecker.onScheduledTick(dt); }, 1.0f, "MessageCheckerScheduler"); \
        } \
        return true; \
    } \
};

BINDING_LAYER_CHECKER(MenuLayer)
BINDING_LAYER_CHECKER(LevelSelectLayer)
BINDING_LAYER_CHECKER(GJGarageLayer)
BINDING_LAYER_CHECKER(GJShopLayer)
BINDING_LAYER_CHECKER(CreatorLayer)
BINDING_LAYER_CHECKER(LevelBrowserLayer)
BINDING_LAYER_CHECKER(LevelInfoLayer)
BINDING_LAYER_CHECKER(LevelEditorLayer)
BINDING_LAYER_CHECKER(PlayLayer)
BINDING_LAYER_CHECKER(SecretRewardsLayer)
BINDING_LAYER_CHECKER(SecretLayer)
BINDING_LAYER_CHECKER(SecretLayer2)
BINDING_LAYER_CHECKER(SecretLayer3)
BINDING_LAYER_CHECKER(SecretLayer4)
BINDING_LAYER_CHECKER(LevelAreaInnerLayer)
BINDING_LAYER_CHECKER(SecretLayer5)
BINDING_LAYER_CHECKER(GauntletSelectLayer)
BINDING_LAYER_CHECKER(GauntletLayer)
