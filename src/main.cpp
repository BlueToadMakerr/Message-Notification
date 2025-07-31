#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    struct Fields {
        bool m_hasBooted = false;
    };

    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delimiter)) {
            result.push_back(item);
        }
        return result;
    }

    void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(),
            "", // empty description since it's not showing properly
            "GJ_messageIcon_001.png",
            false
        );
    }

    void showNotificationLater(CCNode*) {
        showNotification("Latest Message\nSent by: Example\nHello there!");
    }

    void checkMessages(float) {
        log::info("[checkMessages] Checking for new messages...");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::warn("[checkMessages] Account not signed in or GJP2 missing.");
            return;
        }

        log::debug("[checkMessages] accountID: {}, GJP2 size: {}", acc->m_accountID, acc->m_GJP2.size());

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
        log::debug("[onMessageResponse] Response received");

        if (!resp || !resp->isSucceed()) {
            log::warn("[onMessageResponse] Request failed or null response.");
            return;
        }

        std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::debug("[onMessageResponse] Response data: {}", response);

        if (response.empty() || response == "-1") {
            log::info("[onMessageResponse] No messages found.");
            return;
        }

        auto messages = split(response, '|');
        if (messages.empty()) {
            log::info("[onMessageResponse] Message list empty.");
            return;
        }

        auto fields = split(messages.front(), ':');
        if (fields.size() < 3) {
            log::warn("[onMessageResponse] Unexpected message format.");
            return;
        }

        int latestID = std::stoi(fields[0]);
        std::string user = fields[1];
        std::string subject = fields[2];

        log::info("[onMessageResponse] Latest ID: {}, From: {}, Subject: {}", latestID, user, subject);

        int lastSeenID = Mod::get()->getSavedValue<int>("last-message-id", 0);
        log::debug("[onMessageResponse] Saved last ID: {}", lastSeenID);

        std::string title;

        if (messages.size() == 1) {
            // One message: include details in title
            title = fmt::format("New Message!\nSent by: {}\n{}", user, subject);
        } else {
            // Multiple messages: show count only
            title = fmt::format("{} New Messages!", messages.size());
        }

        if (!m_fields->m_hasBooted) {
            log::info("[onMessageResponse] Showing message on boot.");
            showNotification(title);
            Mod::get()->setSavedValue("last-message-id", latestID);
            m_fields->m_hasBooted = true;
            return;
        }

        if (latestID > lastSeenID) {
            log::info("[onMessageResponse] New message detected!");
            showNotification(title);
            Mod::get()->setSavedValue("last-message-id", latestID);
        } else {
            log::debug("[onMessageResponse] No new messages.");
        }
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        log::info("[init] MessageChecker initialized.");
        this->schedule(schedule_selector(MessageChecker::checkMessages), 300.0f);
        this->checkMessages(0);

        // Show test notification after 1 second (boot-only)
        this->runAction(
            cocos2d::CCSequence::create(
                cocos2d::CCDelayTime::create(1.0f),
                cocos2d::CCCallFuncN::create(this, callfuncN_selector(MessageChecker::showNotificationLater)),
                nullptr
            )
        );

        return true;
    }
};