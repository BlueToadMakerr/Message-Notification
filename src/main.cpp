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
        return result;
    }

    void onMessageResponse(cocos2d::extension::CCHttpClient* client, cocos2d::extension::CCHttpResponse* resp) {
        log::debug("[onMessageResponse] Called");

        if (!resp || !resp->isSucceed()) {
            log::warn("[onMessageResponse] Failed or null response");
            return;
        }

        std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::debug("[onMessageResponse] Response: {}", response);

        if (response.empty() || response == "-1") {
            log::info("[onMessageResponse] No new messages");
            return;
        }

        std::vector<std::string> messages = split(response, '|');
        static size_t lastCount = 0;
        log::info("[onMessageResponse] Message count: {}, Last count: {}", messages.size(), lastCount);

        if (messages.size() <= lastCount) {
            log::info("[onMessageResponse] No new messages since last check");
            return;
        }

        size_t newMessages = messages.size() - lastCount;
        lastCount = messages.size();

        std::string title = newMessages == 1 ? "New Message!" : fmt::format("{} New Messages!", newMessages);
        std::string desc = "Check your inbox!";
        std::string icon = "GJ_messageIcon_001.png";

        if (newMessages == 1) {
            auto fields = split(messages.back(), ':');
            if (fields.size() >= 3) {
                std::string user = fields[1];
                std::string subject = fields[2];
                desc = fmt::format("From: {}\n{}", user, subject);
                log::info("[onMessageResponse] Message from: {} - {}", user, subject);
            }
        }

        AchievementNotifier::sharedState()->notifyAchievement(title.c_str(), desc.c_str(), icon.c_str(), false);
        log::debug("[onMessageResponse] Notification displayed");
    }

    void checkMessages(float) {
        log::debug("[checkMessages] Running message check");

        auto* acc = GJAccountManager::sharedState();
        log::debug("[checkMessages] accountID: {}, GJP2 size: {}", acc->m_accountID, acc->m_GJP2.size());

        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::warn("[checkMessages] Invalid account ID or empty GJP2");
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
        req->setResponseCallback(this, callfuncND_selector(MessageChecker::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();

        log::debug("[checkMessages] Request sent");
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        log::info("[MessageChecker] init called - starting message check timer");
        this->schedule(schedule_selector(MessageChecker::checkMessages), 300.0f);

        // Trigger once on boot to verify it works
        this->checkMessages(0);
        return true;
    }
};