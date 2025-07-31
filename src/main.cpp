#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    void checkMessages() {
        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) return;

        std::string postData = fmt::format(
            "accountID={}&gjp2={}&secret=Wmfd2893gb7",
            acc->m_accountID, acc->m_GJP2
        );

        auto req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::Type::POST);
        req->setRequestData(postData.c_str(), postData.size());
        req->setResponseCallback([](cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
            if (!resp || !resp->isSucceed()) return;

            std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
            if (response.empty() || response == "-1") return;

            std::vector<std::string> messages = split(response, '|');
            static size_t lastCount = 0;
            if (messages.size() <= lastCount) return; // no new messages

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
                }
            }

            AchievementNotifier::sharedState()->notifyAchievement(title, desc, icon);
        });

        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        this->schedule(schedule_selector(MessageChecker::checkMessages), 300.0f); // every 5 minutes
        return true;
    }
};