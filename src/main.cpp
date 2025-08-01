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

    if (currentMessages.empty()) {
        log::info("[onMessageResponse] No messages to process.");
        return;
    }

    auto savedData = Mod::get()->getSavedValue<std::string>("last-messages", "");
    log::info("[onMessageResponse] Loaded previous messages from save: {}", savedData);

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
                log::info("[onMessageResponse] Showing single message notification");
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
