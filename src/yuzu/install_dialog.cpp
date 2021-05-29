// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <optional>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

#include "core/core.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs.h"
#include "core/loader/loader.h"

#include "yuzu/install_dialog.h"
#include "yuzu/uisettings.h"

namespace {

std::string TitleTypeToString(FileSys::TitleType title_type) {
    switch (title_type) {
    case FileSys::TitleType::Update:
        return "Update";
    case FileSys::TitleType::AOC:
        return "DLC";
    default:
        return "";
    }
}

std::optional<FileSys::CNMT> GetCNMT(std::shared_ptr<FileSys::NCA> meta_nca) {
    if (meta_nca->GetSubdirectories().empty()) {
        return std::nullopt;
    }

    const auto section0 = meta_nca->GetSubdirectories()[0];

    if (section0->GetFiles().empty()) {
        return std::nullopt;
    }

    return FileSys::CNMT{section0->GetFiles()[0]};
}

std::optional<FileSys::NACP> GetNACP(std::shared_ptr<FileSys::NCA> control_nca) {
    const auto romfs = control_nca->GetRomFS();

    if (romfs == nullptr) {
        return std::nullopt;
    }

    const auto extracted = FileSys::ExtractRomFS(romfs);

    if (extracted == nullptr) {
        return std::nullopt;
    }

    auto nacp_file = extracted->GetFile("control.nacp");

    if (nacp_file == nullptr) {
        nacp_file = extracted->GetFile("Control.nacp");
    }

    if (nacp_file == nullptr) {
        return std::nullopt;
    }

    return FileSys::NACP{nacp_file};
}

} // namespace

InstallDialog::InstallDialog(QWidget* parent, FileSys::VirtualFilesystem vfs,
                             const QStringList& files)
    : QDialog(parent) {
    auto& system = Core::System::GetInstance();

    file_list = new QListWidget(this);

    for (const QString& file : files) {
        const auto file_path = file.toStdString();
        const auto v_file = vfs->OpenFile(file.toStdString(), FileSys::Mode::Read);

        if (v_file == nullptr) {
            continue;
        }

        if (file.endsWith(QStringLiteral("nca"), Qt::CaseInsensitive)) {
            AddItem(file, QFileInfo(file).fileName());
            continue;
        }

        std::shared_ptr<FileSys::NSP> nsp;

        if (file.endsWith(QStringLiteral("xci"), Qt::CaseInsensitive)) {
            const auto xci = std::make_shared<FileSys::XCI>(v_file);
            nsp = xci->GetSecurePartitionNSP();
        } else if (file.endsWith(QStringLiteral("nsp"), Qt::CaseInsensitive)) {
            nsp = std::make_shared<FileSys::NSP>(v_file);
        } else {
            continue;
        }

        if (nsp == nullptr || nsp->GetStatus() != Loader::ResultStatus::Success) {
            continue;
        }

        const auto ncas = nsp->GetNCAsCollapsed();

        const auto meta_iter = std::find_if(ncas.begin(), ncas.end(), [](const auto& nca) {
            return nca->GetType() == FileSys::NCAContentType::Meta;
        });

        const auto control_iter = std::find_if(ncas.begin(), ncas.end(), [](const auto& nca) {
            return nca->GetType() == FileSys::NCAContentType::Control;
        });

        if (meta_iter == ncas.end()) {
            continue;
        }

        const auto cnmt = GetCNMT(*meta_iter);

        if (!cnmt.has_value()) {
            continue;
        }

        const auto title_type_str = TitleTypeToString(cnmt.value().GetType());

        if (title_type_str.empty()) {
            continue;
        }

        const auto nacp = control_iter == ncas.end() ? std::nullopt : GetNACP(*control_iter);

        if (!nacp.has_value()) {
            const auto formatted_name = fmt::format("{} ({}) (v{})", nsp->GetName(), title_type_str,
                                                    cnmt.value().GetTitleVersion());

            AddItem(file, QString::fromStdString(formatted_name));
        } else {
            const auto formatted_name =
                fmt::format("{} ({}) ({})", nacp.value().GetApplicationName(), title_type_str,
                            nacp.value().GetVersionString());

            AddItem(file, QString::fromStdString(formatted_name));
        }
    }

    file_list->setMinimumWidth((file_list->sizeHintForColumn(0) * 11) / 10);

    vbox_layout = new QVBoxLayout;
    hbox_layout = new QHBoxLayout;

    description = new QLabel(tr("Please confirm these are the files you wish to install."));

    update_description =
        new QLabel(tr("Installing an Update or DLC will overwrite the previously installed one."));

    buttons = new QDialogButtonBox;
    buttons->addButton(QDialogButtonBox::Cancel);
    buttons->addButton(tr("Install"), QDialogButtonBox::AcceptRole);

    connect(buttons, &QDialogButtonBox::accepted, this, &InstallDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &InstallDialog::reject);

    hbox_layout->addWidget(buttons);

    vbox_layout->addWidget(description);
    vbox_layout->addWidget(update_description);
    vbox_layout->addWidget(file_list);
    vbox_layout->addLayout(hbox_layout);

    setLayout(vbox_layout);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("Install Files to NAND"));
}

InstallDialog::~InstallDialog() = default;

QStringList InstallDialog::GetFiles() const {
    QStringList files;

    for (int i = 0; i < file_list->count(); ++i) {
        const QListWidgetItem* item = file_list->item(i);
        if (item->checkState() == Qt::Checked) {
            files.append(item->data(Qt::UserRole).toString());
        }
    }

    return files;
}

int InstallDialog::GetMinimumWidth() const {
    return file_list->width();
}

void InstallDialog::AddItem(const QString& file, const QString& formatted_name) {
    QListWidgetItem* item = new QListWidgetItem(formatted_name, file_list);
    item->setData(Qt::UserRole, file);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
}
