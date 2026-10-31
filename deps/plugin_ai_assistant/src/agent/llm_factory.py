from __future__ import annotations

from langchain_core.language_models.chat_models import BaseChatModel
from langchain_openai import ChatOpenAI
from langchain_anthropic import ChatAnthropic
from ..config import AssistantConfig

DEFAULT_MODELS = {
    "ollama": "qwen2.5-coder:7b",
    "openai": "gpt-4o",
    "gemini": "gemini-2.5-flash",
    "anthropic": "claude-3-7-sonnet-latest",
}


def create_chat_model(config: AssistantConfig) -> BaseChatModel:
    provider = config.provider
    model = config.model_name.strip() or DEFAULT_MODELS.get(provider, "gpt-4o")

    if provider == "ollama":
        return ChatOpenAI(
            model=model,
            base_url=config.ollama_base_url,
            api_key="ollama",
            temperature=0.1,
        )
    if provider == "gemini":
        key = config.api_keys.get("gemini", "") or "missing-key"
        return ChatOpenAI(
            model=model,
            base_url=config.gemini_base_url,
            api_key=key,
            temperature=0.1,
        )
    if provider == "anthropic":
        key = config.api_keys.get("anthropic", "") or "missing-key"
        return ChatAnthropic(
            model_name=model,
            api_key=key,
            temperature=0.1,
        )
    key = config.api_keys.get("openai", "") or "missing-key"
    return ChatOpenAI(
        model=model,
        base_url=config.openai_base_url,
        api_key=key,
        temperature=0.1,
    )
