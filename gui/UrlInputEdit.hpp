#ifndef URL_INPUT_EDIT_HPP
#define URL_INPUT_EDIT_HPP

#include <QTextEdit>

/** 粘贴时优先写入真实 http(s) URL，避免富文本链接显示成直播标题。 */
class UrlInputEdit final : public QTextEdit {
    Q_OBJECT

public:
    explicit UrlInputEdit(QWidget *parent = nullptr);

protected:
    void insertFromMimeData(const QMimeData *source) override;
};

#endif
