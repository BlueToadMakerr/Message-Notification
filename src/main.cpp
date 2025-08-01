#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    struct Fields {
        bool m_hasChecked = false;
    };

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
        auto parts = split(msg, ':');
        std::stringstream sanitized;

        // Reconstruct message excluding tags 7 (time) and 8 (read status)
        for (size_t i = 0; i + 1 < parts.size(); i += 2) {
            const std::string& key = parts[i];
            const std::string& val = parts[i + 1];
            if (key != "7" && key != "8") {
                sanitized << key << ":" << val << ":";
            }
        }

        std::string result = sanitized.str();
        if (!result.empty() && result.back() == ':') {
            result.pop_back();
        }
        return result;
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
        log::info("[MessageChecker] Starting message check...");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("[MessageChecker] Not signed in.");
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
        log::info("[onMessageResponse] Network response received");

        if (!resp || !resp->isSucceed()) {
            log::info("[onMessageResponse] Request failed.");
            return;
        }

        std::string rawResponse(resp->getResponseData()->begin(), resp->getResponseData()->end());
        if (rawResponse.empty() || rawResponse == "-1") {
            log::info("[onMessageResponse] No messages.");
            return;
        }

        auto rawMessages = split(rawResponse, '|');
        std::vector<std::string> sanitizedMessages;
        for (const auto& m : rawMessages) {
            sanitizedMessages.push_back(sanitizeMessage(m));
        }

        auto savedRaw = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto savedMessages = split(savedRaw, '|');

        std::vector<std::string> newMessages;
        for (const auto& msg : sanitizedMessages) {
            if (std::find(savedMessages.begin(), savedMessages.end(), msg) == savedMessages.end()) {
                newMessages.push_back(msg);
            }
        }

        if (!newMessages.empty()) {
            if (newMessages.size() == 1) {
                auto parts = split(newMessages[0], ':');
                std::string sender = "<unknown>";
                std::string subject = "<no subject>";

                for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                    if (parts[i] == "6") sender = parts[i + 1];
                    else if (parts[i] == "4") {
                        auto base64 = parts[i + 1];
                        log::info("[onMessageResponse] Raw base64 subject: {}", base64);
                        auto decoded = geode::utils::base64::decode(base64);
                        if (decoded) {
                            auto& data = decoded.unwrap();
                            subject = std::string(data.begin(), data.end());
                        } else {
                            subject = "<invalid base64>";
                        }
                    }
                }

                std::string title = fmt::format("New Message!\nSent by: {}\n{}", sender, subject);
                showNotification(title);
            } else {
                std::string title = fmt::format("{} New Messages!", newMessages.size());
                showNotification(title);
            }
        }

        // Save current sanitized messages
        std::string toSave;
        for (size_t i = 0; i < sanitizedMessages.size(); ++i) {
            toSave += sanitizedMessages[i];
            if (i + 1 < sanitizedMessages.size()) toSave += "|";
        }

        Mod::get()->setSavedValue("last-messages", toSave);
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        if (!m_fields->m_hasChecked) {
            m_fields->m_hasChecked = true;

            log::info("[init] Initializing and scheduling message checks.");

            // Schedule the check based on the stored setting
            this->schedule([this](float) {
                this->checkMessages(0);

                // Reschedule with latest interval
                this->unscheduleAllSelectors();
                int newInterval = std::clamp(Mod::get()->getSavedValue<int>("check-interval", 300), 60, 600);
                this->schedule(schedule_selector(MessageChecker::checkMessages), static_cast<float>(newInterval));
            }, 0.1f, 0, 0); // Initial delay of 0.1s
        }

        return true;
    }
};
