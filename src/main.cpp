#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>
#include <sstream>

using namespace geode::prelude;

// Layer that checks messages
class MessageCheckerLayer : public cocos2d::CCLayer {
protected:
    std::chrono::steady_clock::time_point nextCheck;

    bool init() override {
        log::info("[CheckerLayer] init");
        nextCheck = std::chrono::steady_clock::now();
        this->schedule(schedule_selector(MessageCheckerLayer::onTick), 1.0f);
        return true;
    }

    void onTick(float) {
        int interval = 300;
        try {
            interval = Mod::get()->getSettingValue<int>("check-interval");
            interval = std::clamp(interval, 60, 600);
        } catch (...) {
            log::info("[CheckerLayer] No interval setting found. Using default 300s.");
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= nextCheck) {
            log::info("[CheckerLayer] Time to check messages.");
            checkMessages();
            nextCheck = now + std::chrono::seconds(interval);
        }
    }

    void checkMessages() {
        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("[CheckerLayer] Not logged in. Skipping message check.");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);
        log::info("[CheckerLayer] Sending HTTP request: {}", postData);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.length());
        req->setTag("MessageCheck");

        req->setResponseCallback(this, SEL_HttpResponse(&MessageCheckerLayer::onResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    static std::vector<std::string> split(const std::string& str, char delim) {
        std::stringstream ss(str);
        std::string item;
        std::vector<std::string> elems;
        while (std::getline(ss, item, delim)) elems.push_back(item);
        return elems;
    }

    static std::string cleanMessage(const std::string& msg) {
        std::vector<std::string> parts = split(msg, ':');
        std::string cleaned;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i] == "7" || parts[i] == "8") { ++i; continue; }
            if (!cleaned.empty()) cleaned += ":";
            cleaned += parts[i];
        }
        return cleaned;
    }

    void onResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        if (!resp || !resp->isSucceed()) {
            log::info("[CheckerLayer] Request failed or response was null.");
            return;
        }

        std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("[CheckerLayer] Raw server response: {}", raw);

        if (raw.empty() || raw == "-1") {
            log::info("[CheckerLayer] No new messages.");
            return;
        }

        auto currentRaw = split(raw, '|');
        std::vector<std::string> currentClean;
        for (auto& msg : currentRaw) {
            auto cleaned = cleanMessage(msg);
            currentClean.push_back(cleaned);
            log::info("[CheckerLayer] Cleaned: {}", cleaned);
        }

        std::string saved = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto old = split(saved, '|');

        std::vector<std::string> newMessages;
        for (auto& clean : currentClean)
            if (std::find(old.begin(), old.end(), clean) == old.end())
                newMessages.push_back(clean);

        if (!newMessages.empty()) {
            log::info("[CheckerLayer] Found {} new messages.", newMessages.size());

            if (newMessages.size() == 1) {
                auto parts = split(newMessages[0], ':');
                std::string subject = "<Invalid>";
                if (parts.size() > 9) {
                    auto decoded = geode::utils::base64::decode(parts[9]);
                    if (decoded.isOk()) {
                        auto vec = decoded.unwrap();
                        subject = std::string(vec.begin(), vec.end());
                    }
                }
                notify(fmt::format("New Message!\n{}", subject));
            } else {
                notify(fmt::format("{} New Messages!", newMessages.size()));
            }
        }

        std::string save;
        for (size_t i = 0; i < currentClean.size(); ++i) {
            if (i > 0) save += "|";
            save += currentClean[i];
        }
        Mod::get()->setSavedValue("last-messages", save);
        log::info("[CheckerLayer] Saved updated message state.");
    }

    void notify(const std::string& msg) {
        log::info("[CheckerLayer] Notifying: {}", msg);
        AchievementNotifier::sharedState()->notifyAchievement(
            msg.c_str(), "", "GJ_messageIcon_001.png", false
        );
    }

public:
    static MessageCheckerLayer* create() {
        auto ret = new MessageCheckerLayer();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

static MessageCheckerLayer* g_checkerLayer = nullptr;

void injectCheckerIntoScene(cocos2d::CCScene* scene) {
    if (!scene) return;

    if (!g_checkerLayer) {
        log::info("[Inject] Creating MessageCheckerLayer.");
        g_checkerLayer = MessageCheckerLayer::create();
        g_checkerLayer->retain();
    }

    if (g_checkerLayer->getParent()) {
        g_checkerLayer->removeFromParentAndCleanup(false);
    }

    log::info("[Inject] Attaching MessageCheckerLayer to new scene.");
    scene->addChild(g_checkerLayer, 99999);
}

// Hook director to inject checker into each new scene
class $modify(MyCCDirector, cocos2d::CCDirector) {
    void replaceScene(cocos2d::CCScene* scene) {
        log::info("[DirectorHook] replaceScene called.");
        injectCheckerIntoScene(scene);
        CCDirector::replaceScene(scene);
    }

    void pushScene(cocos2d::CCScene* scene) {
        log::info("[DirectorHook] pushScene called.");
        injectCheckerIntoScene(scene);
        CCDirector::pushScene(scene);
    }
};

// Verify mod loads
$on_mod(Loaded) {
    log::info("[Mod] Message checker mod loaded successfully.");
}
