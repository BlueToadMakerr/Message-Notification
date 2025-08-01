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

    // Removes dynamic fields like 7:<...>
    static std::string stripDynamicFields(const std::string& msg) {
        auto parts = split(msg, ':');
        std::string stripped;

        for (size_t i = 0; i + 1 < parts.size(); i += 2) {
            if (parts[i] == "7") continue; // skip dynamic field
            if (!stripped.empty()) stripped += ":";
            stripped += parts[i] + ":" + parts[i + 1];
        }

        return stripped;
    }

    void showNotification(const std::string& title) {
        log::info("[showNotification] Showing notification with title:\n{}", title);
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(),
            "", // No description
            "GJ_messageIcon_001.png",
            false
        );
    }

    void checkMessages(float) {
        log::info("[checkMessages] Checking for new messages...");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("[checkMessages] Account not signed in or GJP2 missing.");
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
        log::info("[onMessageResponse] Network response received.");

        if (!resp || !resp->isSucceed()) {
            log::info("[onMessageResponse] Request failed or null response.");
            return;
        }

        std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("[onMessageResponse] Raw response:\n{}", response);

        if (response.empty() || response == "-1") {
            log::info("[onMessageResponse] No messages found.");
            return;
        }

        auto currentMessages = split(response, '|');
        if (currentMessages.empty()) {
            log::info("[onMessageResponse] No parsable messages.");
            return;
        }

        auto savedData = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto previousMessagesRaw = split(savedData, '|');

        // Strip dynamic fields
        std::vector<std::string> previousMessages;
        for (const auto& m : previousMessagesRaw)
            previousMessages.push_back(stripDynamicFields(m));

        std::vector<std::string> newMessages;
        std::vector<std::string> strippedMessages; // for saving later

        for (const auto& msg : currentMessages) {
            auto stripped = stripDynamicFields(msg);
            strippedMessages.push_back(stripped);

            if (std::find(previousMessages.begin(), previousMessages.end(), stripped) == previousMessages.end()) {
                newMessages.push_back(msg);
            }
        }

        if (!newMessages.empty()) {
            log::info("[onMessageResponse] {} new message(s) detected", newMessages.size());

            if (newMessages.size() == 1) {
                log::info("[onMessageResponse] Handling single message...");
                auto parts = split(newMessages[0], ':');
                if (parts.size() >= 5) {
                    std::string user = parts[1];
                    std::string subjectBase64 = parts[9];
                    log::info("[onMessageResponse] Raw base64 subject: {}", subjectBase64);


                    auto decodeResult = geode::utils::base64::decode(subjectBase64);
                    std::string subject;
                    if (decodeResult) {
                        auto& decoded = decodeResult.unwrap();
                        subject = std::string(decoded.begin(), decoded.end());
                        log::info("[onMessageResponse] Decoded subject: {}", subject);
                    } else {
                        subject = "<invalid base64>";
                        log::info("[onMessageResponse] Failed to decode subject base64");
                    }

                    std::string title = fmt::format("New Message!\nSent by: {}\n{}", user, subject);
                    showNotification(title);
                } else {
                    log::info("[onMessageResponse] Invalid message format, fallback notification");
                    showNotification("New Message!");
                }
            } else {
                std::string title = fmt::format("{} New Messages!", newMessages.size());
                showNotification(title);
            }
        } else {
            log::info("[onMessageResponse] No new messages compared to last fetch.");
        }

        // Save only stripped messages
        std::string saveData;
        for (size_t i = 0; i < strippedMessages.size(); ++i) {
            if (i > 0) saveData += "|";
            saveData += strippedMessages[i];
        }
        Mod::get()->setSavedValue("last-messages", saveData);
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        log::info("[init] MessageChecker initialized.");

        int interval = std::clamp(Mod::get()->getSavedValue<int>("check-interval", 300), 60, 600);
        log::info("[init] Using check interval (seconds): {}", interval);

        this->schedule(schedule_selector(MessageChecker::checkMessages), static_cast<float>(interval));
        this->checkMessages(0);
        return true;
    }
};
