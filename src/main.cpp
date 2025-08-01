#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    struct Fields {
        bool hasChecked = false;
    };

    // Utility to split strings
    static std::vector<std::string> split(const std::string& str, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }

    // Remove time and read tags before saving
    static std::string cleanMessageString(const std::string& raw) {
        std::vector<std::string> messages = split(raw, '|');
        std::vector<std::string> cleaned;
        for (auto& msg : messages) {
            auto parts = split(msg, ':');
            std::string cleanedMsg;
            for (size_t i = 0; i < parts.size(); i += 2) {
                if (parts[i] == "7" || parts[i] == "8") continue;
                if (i + 1 < parts.size()) {
                    cleanedMsg += parts[i] + ":" + parts[i + 1] + ":";
                }
            }
            if (!cleanedMsg.empty() && cleanedMsg.back() == ':')
                cleanedMsg.pop_back();
            cleaned.push_back(cleanedMsg);
        }
        return geode::utils::string::join(cleaned, "|");
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

        // Dynamically reload check interval (for rescheduling)
        int interval = std::clamp(Mod::get()->getSavedValue<int>("check-interval", 300), 60, 600);
        this->unschedule(schedule_selector(MessageChecker::checkMessages));
        this->schedule(schedule_selector(MessageChecker::checkMessages), static_cast<float>(interval));

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
        log::info("[MessageChecker] Response received.");

        if (!resp || !resp->isSucceed()) {
            log::info("[MessageChecker] Request failed.");
            return;
        }

        std::string rawResponse(resp->getResponseData()->begin(), resp->getResponseData()->end());
        if (rawResponse.empty() || rawResponse == "-1") {
            log::info("[MessageChecker] No messages returned.");
            return;
        }

        std::string cleanedResponse = cleanMessageString(rawResponse);
        auto currentMessages = split(cleanedResponse, '|');
        auto savedRaw = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto previousMessages = split(savedRaw, '|');

        std::vector<std::string> newMessages;
        for (auto& msg : currentMessages) {
            if (std::find(previousMessages.begin(), previousMessages.end(), msg) == previousMessages.end()) {
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
                    if (parts[i] == "4") {
                        std::string encoded = parts[i + 1];
                        log::info("[MessageChecker] Raw base64: {}", encoded);
                        auto decoded = geode::utils::base64::decode(encoded);
                        if (decoded) {
                            subject = std::string(decoded.unwrap().begin(), decoded.unwrap().end());
                        } else {
                            subject = "<invalid base64>";
                        }
                    }
                }

                std::string title = fmt::format("New Message!\nSent by: {}\n{}", sender, subject);
                showNotification(title);
            } else {
                showNotification(fmt::format("{} New Messages!", newMessages.size()));
            }
        }

        Mod::get()->setSavedValue("last-messages", cleanedResponse);
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        if (!m_fields->hasChecked) {
            m_fields->hasChecked = true;

            log::info("[MessageChecker] Initializing message checker...");

            int interval = std::clamp(Mod::get()->getSavedValue<int>("check-interval", 300), 60, 600);
            this->schedule(schedule_selector(MessageChecker::checkMessages), static_cast<float>(interval));
            this->checkMessages(0);
        }

        return true;
    }
};
