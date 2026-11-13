const copyButtons = document.querySelectorAll("[data-copy]");

function fallbackCopy(text) {
    const textarea = document.createElement("textarea");
    textarea.value = text;
    textarea.style.position = "fixed";
    textarea.style.opacity = "0";
    document.body.appendChild(textarea);
    textarea.select();
    const copied = document.execCommand("copy");
    textarea.remove();

    if (!copied) throw new Error("Copy command failed");
}

copyButtons.forEach((button) => {
    button.addEventListener("click", async () => {
        const source = document.getElementById(button.dataset.copy);
        if (!source) return;

        try {
            if (navigator.clipboard && window.isSecureContext) {
                await navigator.clipboard.writeText(source.textContent);
            } else {
                fallbackCopy(source.textContent);
            }
            button.textContent = "Скопировано";
            button.classList.add("copied");
        } catch {
            button.textContent = "Не удалось";
        }

        window.setTimeout(() => {
            button.textContent = "Копировать";
            button.classList.remove("copied");
        }, 2000);
    });
});
