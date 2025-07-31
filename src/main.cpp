#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <cocos2d.h>
#include <cocos-ext.h>

using namespace geode::prelude;

class MessageChecker : public CCObject {
public:
    static MessageChecker* create() {
        auto ret = new MessageChecker();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() {
        scheduleSelector(schedule_selector(MessageChecker::checkMessages), 300.0f); // every 5 mins
        this->checkMessages(nullptr); // run once at start
        return true;
    }

    void checkMessages(CCObject*) {
        log::info("Checking for new messages...");

        auto loginName = GameManager::sharedState()->m_accountName;
        auto loginID = GameManager::sharedState()->m_accountID;
        auto gjp2 = GameManager::sharedState()->m_GJP2;

        if (loginID <= 0 || gjp2.empty()) {
            log::warn("Not logged in or missing GJP2.");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", loginID, gjp2);

        auto request = new CCHttpRequest();
        request->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        request->setRequestType(CCHttpRequest::kHttpPost);
        request->setRequestData(postData.c_str(), postData.size());
        request->setResponseCallback(this, httpresponse_selector(MessageChecker::onResponse));
        request->setTag("boot");

        CCHttpClient::getInstance()->send(request);
        request->release();
    }

    void onResponse(CCHttpClient* client, CCHttpResponse* response) {
        if (!response || !response->isSucceed()) {
            log::error("Failed to fetch messages.");
            return;
        }

        std::string resData(response->getResponseData()->begin(), response->getResponseData()->end());
        log::info("Response received: {}", resData);

        std::vector<std::string> messages = utils::string::split(resData, '|');
        if (messages.empty()) {
            log::info("No messages found.");
            return;
        }

        std::vector<std::string> newMessages;
        int lastSeenID = Mod::get()->getSavedValue<int>("last-message-id");
        int newestID = 0;

        for (auto const& msg : messages) {
            auto fields = utils::string::split(msg, ':');
            if (fields.size() < 8) continue;

            int msgID = std::stoi(fields[0]);
            if (msgID > newestID) newestID = msgID;

            if (msgID > lastSeenID) {
                newMessages.push_back(msg);
            }
        }

        log::info("Found {} new messages", newMessages.size());

        if (!newMessages.empty()) {
            Mod::get()->setSavedValue("last-message-id", newestID);

            if (newMessages.size() == 1) {
                auto fields = utils::string::split(newMessages[0], ':');
                std::string user = fields.size() > 1 ? cocos2d::ZipUtils::urlDecode(fields[1]) : "Unknown";
                std::string subject = fields.size() > 2 ? cocos2d::ZipUtils::urlDecode(fields[2]) : "(No subject)";
                AchievementNotifier::sharedState()->notifyAchievement("New Message!", fmt::format("From: {}\n{}", user, subject).c_str(), "GJ_messageIcon_001.png", false);
            } else {
                AchievementNotifier::sharedState()->notifyAchievement(fmt::format("{} New Messages!", newMessages.size()).c_str(), "You have new unread messages", "GJ_messageIcon_001.png", false);
            }
        }
    }
};

class $modify(MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto checker = MessageChecker::create();
        this->addChild(checker);

        return true;
    }
};