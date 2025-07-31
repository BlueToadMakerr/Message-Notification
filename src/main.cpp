#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delimiter)) {
            result.push_back(item);
        }
        log::debug("[split] Input string: '{}', delimiter: '{}', result count: {}", str, delimiter, result.size());
        return result;
    }

    void onMessageResponse(cocos2d::CCObject* sender, void* data) {
        log::info("[onMessageResponse] Received HTTP response callback");

        auto* resp = static_cast<cocos2d::extension::CCHttpResponse*>(data);
        if (!resp) {
            log::error("[onMessageResponse] Response is null");
            return;
        }

        if (!resp->isSucceed()) {
            log::error("[onMessageResponse] Response failed with error: {}", resp->getErrorBuffer());
            return;
        }

        std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::debug("[onMessageResponse] Response string: {}", response);

        if (response.empty() || response == "-1") {
            log::warn("[onMessageResponse] Response is empty or no messages (-1)");
            return;
        }

        std::vector<std::string> messages = split(response, '|');
        log::info("[onMessageResponse] Parsed {} message(s)", messages.size());

        static size_t lastCount = 0;
        if (messages.size() <= lastCount) {
            log::debug("[onMessageResponse] No new messages. Last count: {}, current: {}", lastCount, messages.size());
            return;
        }

        size_t newMessages = messages.size() - lastCount;
        log::info("[onMessageResponse] Detected {} new message(s)", newMessages);
        lastCount = messages.size();

        std::string title = newMessages == 1 ? "New Message!" : fmt::format("{} New Messages!", newMessages);
        std::string desc = "Check your inbox!";
        std::string icon = "GJ_messageIcon_001.png";

        if (newMessages == 1) {
            auto fields = split(messages.back(), ':');
            log::debug("[onMessageResponse] Fields for last message: {}", fields.size());
            if (fields.size() >= 3) {
                std::string user = fields[1];
                std::string subject = fields[2];
                desc = fmt::format("From: {}\n{}", user, subject);
                log::info("[onMessageResponse] Showing notification for 1 message from '{}': '{}'", user, subject);
            } else {
                log::warn("[onMessageResponse] Not enough fields in message for single-message display");
            }
        }

        log::debug("[onMessageResponse] Triggering AchievementNotifier");
        AchievementNotifier::sharedState()->notifyAchievement(title.c_str(), desc.c_str(), icon.c_str(), false);
    }

    void checkMessages(float) {
        log::info("[checkMessages] Checking for new messages...");

        auto* acc = GJAccountManager::sharedState();
        log::debug("[checkMessages] accountID: {}, GJP2 length: {}", acc->m_accountID, acc->m_GJP2.length());

        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::warn("[checkMessages] Invalid account ID or missing GJP2. Skipping check.");
            return;
        }

        std::string postData = fmt::format(
            "accountID={}&gjp2={}&secret=Wmfd2893gb7",
            acc->m_accountID, acc->m_GJP2
        );
        log::debug("[checkMessages] Post data: {}", postData);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.size());
        log::debug("[checkMessages] Setting response callback");
        req->setResponseCallback(this, static_cast<cocos2d::extension::SEL_HttpResponse>(&MessageChecker::onMessageResponse));

        log::info("[checkMessages] Sending HTTP request...");
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    bool init() {
        log::info("[init] Initializing MessageChecker...");
        if (!MenuLayer::init()) {
            log::error("[init] MenuLayer base init failed");
            return false;
        }

        log::debug("[init] Scheduling periodic message checks every 5 minutes");
        this->schedule(schedule_selector(MessageChecker::checkMessages), 300.0f);

        log::info("[init] Performing initial message check immediately");
        this->checkMessages(0.0f);
        return true;
    }
};