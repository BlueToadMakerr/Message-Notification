#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    static std::vector<std::string> split(const std::string& str, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }

    static std::string sanitizeMessage(const std::string& msg) {
        std::vector<std::string> parts = split(msg, ':');
        std::ostringstream cleaned;
        for (size_t i = 0; i < parts.size(); i += 2) {
            std::string key = parts[i];
            if (key == "7" || key == "8") continue; // Skip time and read status
            if (i + 1 < parts.size()) {
                if (!cleaned.str().empty()) cleaned << ":";
                cleaned << key << ":" << parts[i + 1];
            }
        }
        return cleaned.str();
    }

    void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(),
            "",
            "GJ_messageIcon_001.png",
            false
        );
    }

    void checkMessages(float) {
        log::info("[MessageChecker] Running message check...");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("[MessageChecker] Account not signed in.");
            return;
        }

        std::string postData = fmt::format(
            "accountID={}&gjp2={}&secret=Wmfd2893gb7",
            acc->m_accountID, acc->m_GJP2
        );

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.size());
        req->setTag("MessageCheck");

        req->setResponseCallback(this, SEL_HttpResponse(&MessageChecker::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onMessageResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        if (!resp || !resp->isSucceed()) {
            log::info("[onMessageResponse] Request failed.");
            return;
        }

        std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
        if (raw.empty() || raw == "-1") {
            log::info("[onMessageResponse] No messages.");
            return;
        }

        auto currentRawMessages = split(raw, '|');
        std::vector<std::string> currentMessages;
        for (const auto& msg : currentRawMessages) {
            currentMessages.push_back(sanitizeMessage(msg));
        }

        auto savedRaw = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto savedRawMessages = split(savedRaw, '|');
        std::vector<std::string> savedMessages;
        for (const auto& msg : savedRawMessages) {
            savedMessages.push_back(sanitizeMessage(msg));
        }

        std::vector<std::string> newMessages;
        for (const auto& msg : currentMessages) {
            if (std::find(savedMessages.begin(), savedMessages.end(), msg) == savedMessages.end()) {
                newMessages.push_back(msg);
            }
        }

        if (!newMessages.empty()) {
            if (newMessages.size() == 1) {
                std::string subject = "<no subject>";
                std::string sender = "<unknown>";

                auto parts = split(newMessages[0], ':');
                for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                    if (parts[i] == "4") {
                        std::string base64 = parts[i + 1];
                        log::info("[onMessageResponse] Base64 subject: {}", base64);
                        auto decodeResult = geode::utils::base64::decode(base64);
                        if (decodeResult) {
                            auto& decoded = decodeResult.unwrap();
                            subject = std::string(decoded.begin(), decoded.end());
                        } else {
                            log::info("[onMessageResponse] Failed to decode subject: {}", base64);
                        }
                    } else if (parts[i] == "6") {
                        sender = parts[i + 1];
                    }
                }

                std::string title = fmt::format("New Message!\nSent by: {}\n{}", sender, subject);
                showNotification(title);
            } else {
                showNotification(fmt::format("{} New Messages!", newMessages.size()));
            }

            // Save raw, unsanitized version for next time
            Mod::get()->setSavedValue("last-messages", raw);
        }
    }

    bool init() {
        if (!MenuLayer::init()) return false;
    
        static bool hasInitialized = false;
        if (!hasInitialized) {
            hasInitialized = true;

            // Delay setup slightly so the mod doesn't interfere with early menu loads
            geode::utils::scheduleOnce([this](float) {
                int interval = std::clamp(Mod::get()->getSavedValue<int>("check-interval", 300), 60, 600);

                // Clear any previous schedules
                geode::utils::unschedule("MessageCheck", this);

                // Schedule message checks with the latest interval
                geode::utils::schedule([this](float) {
                    int newInterval = std::clamp(Mod::get()->getSavedValue<int>("check-interval", 300), 60, 600);
                    geode::utils::unschedule("MessageCheck", this);
                    geode::utils::schedule([this](float f) { this->checkMessages(f); }, newInterval, "MessageCheck", this);
                    this->checkMessages(0.f);
                }, 1.f, "MessageInitDelay", this);
            }, 0.5f, "MessageStartDelay", this);
        }

        return true;
    }
};