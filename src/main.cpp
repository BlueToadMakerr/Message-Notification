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
    struct Fields {
        bool m_hasBooted = false;
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

    void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(),
            "",
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
        log::info("[onMessageResponse] Response callback triggered");

        if (!resp || !resp->isSucceed()) {
            log::info("[onMessageResponse] Request failed or response is null.");
            return;
        }

        std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("[onMessageResponse] Raw response: {}", response);

        if (response.empty() || response == "-1") {
            log::info("[onMessageResponse] Response is empty or -1 (no messages).");
            return;
        }

        auto currentMessages = split(response, '|');
        log::info("[onMessageResponse] Parsed {} messages from response", currentMessages.size());

        auto savedData = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto previousMessages = split(savedData, '|');
        log::info("[onMessageResponse] Parsed {} saved messages", previousMessages.size());

        std::vector<std::string> newMessages;
        for (const auto& msg : currentMessages) {
            log::info("[onMessageResponse] Checking message: {}", msg);
            if (std::find(previousMessages.begin(), previousMessages.end(), msg) == previousMessages.end()) {
                log::info("[onMessageResponse] Found new message!");
                newMessages.push_back(msg);
            } else {
                log::info("[onMessageResponse] Message is already known.");
            }
        }

        if (!m_fields->m_hasBooted) {
            log::info("[onMessageResponse] First run, skipping notification");
            m_fields->m_hasBooted = true;
        } else if (!newMessages.empty()) {
            log::info("[onMessageResponse] {} new message(s) detected", newMessages.size());

            if (newMessages.size() == 1) {
                log::info("[onMessageResponse] Handling single message...");
                auto parts = split(newMessages[0], ':');
                log::info("[onMessageResponse] Split parts: size = {}", parts.size());

                if (parts.size() >= 5) {
                    std::string user = parts[1];
                    std::string subjectBase64 = parts[4];
                    log::info("[onMessageResponse] User = {}, Subject (b64) = {}", user, subjectBase64);

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
                log::info("[onMessageResponse] Showing multi-message notification");
                std::string title = fmt::format("{} New Messages!", newMessages.size());
                showNotification(title);
            }
        } else {
            log::info("[onMessageResponse] No new messages detected");
        }

        log::info("[onMessageResponse] Saving current messages to persistent storage");
        Mod::get()->setSavedValue("last-messages", response);
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        log::info("[init] MessageChecker initialized.");

        int interval = std::clamp(Mod::get()->getSavedValue<int>("check-interval", 300), 60, 600);
        log::info("[init] Scheduling check every {} seconds", interval);

        this->schedule(schedule_selector(MessageChecker::checkMessages), static_cast<float>(interval));
        this->checkMessages(0);
        return true;
    }
};
