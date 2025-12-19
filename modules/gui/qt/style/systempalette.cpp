/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#include "systempalette.hpp"
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QFile>
#include <maininterface/mainctx.hpp>
#include <vlc_modules.h>
#include <vlc_picture.h>

namespace {

QColor setColorAlpha(const QColor& c1, float alpha)
{
    QColor c(c1);
    c.setAlphaF(alpha);
    return c;
}

#ifndef _WIN32
/**
 * function taken from QtBase gui/platform/unix/qgenericunixservices.cpp
 */
static inline QByteArray detectLinuxDesktopEnvironment()
{
    const QByteArray xdgCurrentDesktop = qgetenv("XDG_CURRENT_DESKTOP");
    if (!xdgCurrentDesktop.isEmpty())
        return xdgCurrentDesktop.toUpper(); // KDE, GNOME, UNITY, LXDE, MATE, XFCE...

    // Classic fallbacks
    if (!qEnvironmentVariableIsEmpty("KDE_FULL_SESSION"))
        return QByteArrayLiteral("KDE");
    if (!qEnvironmentVariableIsEmpty("GNOME_DESKTOP_SESSION_ID"))
        return QByteArrayLiteral("GNOME");

    // Fallback to checking $DESKTOP_SESSION (unreliable)
    QByteArray desktopSession = qgetenv("DESKTOP_SESSION");

    // This can be a path in /usr/share/xsessions
    int slash = desktopSession.lastIndexOf('/');
    if (slash != -1) {
        QSettings desktopFile(QFile::decodeName(desktopSession + ".desktop"), QSettings::IniFormat);
        desktopFile.beginGroup(QStringLiteral("Desktop Entry"));
        QByteArray desktopName = desktopFile.value(QStringLiteral("DesktopNames")).toByteArray();
        if (!desktopName.isEmpty())
            return desktopName;

        // try decoding just the basename
        desktopSession = desktopSession.mid(slash + 1);
    }

    if (desktopSession == "gnome")
        return QByteArrayLiteral("GNOME");
    else if (desktopSession == "xfce")
        return QByteArrayLiteral("XFCE");
    else if (desktopSession == "kde")
        return QByteArrayLiteral("KDE");

    return QByteArrayLiteral("UNKNOWN");
}

bool isGTKBasedEnvironment()
{
    QList<QByteArray> gtkBasedEnvironments{
        "GNOME",
        "X-CINNAMON",
        "UNITY",
        "MATE",
        "XFCE",
        "LXDE"
    };
    const QList<QByteArray> desktopNames = detectLinuxDesktopEnvironment().split(':');
    for (const QByteArray& desktopName: desktopNames)
    {
        if (gtkBasedEnvironments.contains(desktopName))
            return true;
    }
    return false;
}
#endif


static void PaletteChangedCallback(vlc_qt_theme_provider_t*, void* data)
{
    auto priv = static_cast<ExternalPaletteImpl*>(data);
    emit priv->paletteChanged();
}

static void MetricsChangedCallback(vlc_qt_theme_provider_t*, vlc_qt_theme_image_type type, void* data)
{
    auto priv = static_cast<ExternalPaletteImpl*>(data);
    assert(priv);
    priv->updateMetrics(type);
}


static void ReleaseVLCPictureCb(void* data)
{
    auto pic = static_cast<picture_t*>(data);
    if (pic)
        picture_Release(pic);
}


static void setColorRBGAInt(
    vlc_qt_theme_provider_t* obj,
    vlc_qt_theme_color_set set, vlc_qt_theme_color_section section,
    vlc_qt_theme_color_name name, vlc_qt_theme_color_state state,
    int r, int g, int b, int a)
{
    auto palette = static_cast<SystemPalette*>(obj->setColorData);
    QColor color(r,g,b,a);
    palette->setColor(
        static_cast<ColorContext::ColorSet>(set), static_cast<ColorContext::ColorSection>(section),
        static_cast<ColorContext::ColorName>(name), static_cast<ColorContext::ColorState>(state),
        color);
}

static void setColorRBGAFloat(
    vlc_qt_theme_provider_t* obj,
    vlc_qt_theme_color_set set, vlc_qt_theme_color_section section,
    vlc_qt_theme_color_name name, vlc_qt_theme_color_state state,
    double r, double g, double b, double a)
{
    auto palette = static_cast<SystemPalette*>(obj->setColorData);
    QColor color;
    color.setRgbF(r,g,b,a);
    palette->setColor(
        static_cast<ColorContext::ColorSet>(set), static_cast<ColorContext::ColorSection>(section),
        static_cast<ColorContext::ColorName>(name), static_cast<ColorContext::ColorState>(state),
        color);
}

}

//ExternalPaletteImpl

ExternalPaletteImpl::ExternalPaletteImpl(MainCtx* ctx, SystemPalette& palette, QObject* parent)
    : QObject(parent)
    , m_palette(palette)
    , m_ctx(ctx)
{
}

ExternalPaletteImpl::~ExternalPaletteImpl()
{
    if (m_provider)
    {
        if (m_provider->close)
            m_provider->close(m_provider);
        if (m_module)
            module_unneed(m_provider, m_module);
        vlc_object_delete(m_provider);
    }
}

bool ExternalPaletteImpl::init()
{
    QString preferedProvider;
#ifndef _WIN32
    if (isGTKBasedEnvironment())
        preferedProvider = "qt-themeprovider-gtk";
#endif

    m_provider = vlc_object_create<vlc_qt_theme_provider_t>(m_ctx->getIntf());
    if (!m_provider)
        return false;


    m_provider->paletteUpdated = PaletteChangedCallback;
    m_provider->paletteUpdatedData = this;

    m_provider->metricsUpdated = MetricsChangedCallback;
    m_provider->metricsUpdatedData = this;

    m_provider->setColorF = setColorRBGAFloat;
    m_provider->setColorInt = setColorRBGAInt;
    m_provider->setColorData = &m_palette;

    m_module = module_need(m_provider, "qt theme provider",
                           preferedProvider.isNull() ? nullptr : qtu(preferedProvider),
                           true);
    if (!m_module)
        return false;
    return true;
}

bool ExternalPaletteImpl::isThemeDark() const
{
    if (!m_provider->isThemeDark)
        return false;
    return m_provider->isThemeDark(m_provider);
}

bool ExternalPaletteImpl::hasCSDImages() const
{
    if (!m_provider->supportThemeImage)
        return false;
    return m_provider->supportThemeImage(m_provider, VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON);
}


QImage ExternalPaletteImpl::getCSDImage(vlc_qt_theme_csd_button_type type, vlc_qt_theme_csd_button_state state, bool maximized, bool active, int bannerHeight)
{
    if (!m_provider->getThemeImage)
        return {};
    vlc_qt_theme_image_setting imageSettings;
    imageSettings.windowScaleFactor = m_ctx->intfMainWindow()->devicePixelRatio();
    imageSettings.userScaleFacor = m_ctx->getIntfUserScaleFactor();
    imageSettings.u.csdButton.buttonType = type;
    imageSettings.u.csdButton.state = state;
    imageSettings.u.csdButton.maximized = maximized;
    imageSettings.u.csdButton.active = active;
    imageSettings.u.csdButton.bannerHeight = bannerHeight;
    picture_t* pic = m_provider->getThemeImage(m_provider, VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON, &imageSettings);
    if (!pic)
        return {};

    QImage::Format format = QImage::Format_Invalid;
    switch (pic->format.i_chroma)
    {
    case VLC_CODEC_ARGB:
        format = QImage::Format_ARGB32_Premultiplied;
        break;
    case VLC_CODEC_RGBA:
        format = QImage::Format_RGBA8888_Premultiplied;
        break;
    case VLC_CODEC_RGB24:
        format = QImage::Format_RGB888;
        break;
    default:
        msg_Err(m_ctx->getIntf(), "unexpected image format from theme provider");
        break;
    }

    return QImage(pic->p[0].p_pixels,
            pic->format.i_visible_width, pic->format.i_visible_height, pic->p[0].i_pitch,
            format,
            ReleaseVLCPictureCb, pic
            );
}

CSDMetrics* ExternalPaletteImpl::getCSDMetrics() const
{
    return m_csdMetrics.get();
}

int ExternalPaletteImpl::update()
{
    if (m_provider->updatePalette)
        return m_provider->updatePalette(m_provider);
    return VLC_EGENERIC;
}

void ExternalPaletteImpl::updateMetrics(vlc_qt_theme_image_type type)
{
    if (m_provider->getThemeMetrics)
    {
        if (type == VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON)
        {
            m_csdMetrics.reset();
            [&](){
                if (!m_provider->getThemeMetrics)
                    return;
                vlc_qt_theme_metrics metrics;
                memset(&metrics, 0, sizeof(metrics));
                bool ret = m_provider->getThemeMetrics(m_provider, VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON, &metrics);
                if (!ret)
                    return;
                m_csdMetrics = std::make_unique<CSDMetrics>();
                m_csdMetrics->interNavButtonSpacing = metrics.u.csd.interNavButtonSpacing;

                m_csdMetrics->csdFrameMarginLeft = metrics.u.csd.csdFrameMarginLeft;
                m_csdMetrics->csdFrameMarginRight = metrics.u.csd.csdFrameMarginRight;
                m_csdMetrics->csdFrameMarginTop = metrics.u.csd.csdFrameMarginTop;
                m_csdMetrics->csdFrameMarginBottom = metrics.u.csd.csdFrameMarginBottom;
            }();
            emit CSDMetricsChanged();
        }
    }
}

SystemPalette::SystemPalette(QObject* parent)
    : QObject(parent)
{
    updatePalette();
}

ColorSchemeModel::ColorScheme SystemPalette::source() const
{
    return m_source;
}

QImage SystemPalette::getCSDImage(vlc_qt_theme_csd_button_type type, vlc_qt_theme_csd_button_state state, bool maximized, bool active, int bannerHeight)
{
    if (!hasCSDImage())
        return QImage();
    assert(m_palettePriv);
    return m_palettePriv->getCSDImage(type, state, maximized, active, bannerHeight);

}

CSDMetrics* SystemPalette::getCSDMetrics() const
{
    if (!m_palettePriv)
        return nullptr;
    return m_palettePriv->getCSDMetrics();
}

bool SystemPalette::hasCSDImage() const
{
    if (!m_palettePriv)
        return false;
    return m_palettePriv->hasCSDImages();
}

void SystemPalette::setSource(ColorSchemeModel::ColorScheme source)
{
    if (m_source == source)
        return;
    m_source = source;

    updatePalette();
    emit sourceChanged();
}

void SystemPalette::setCtx(MainCtx* ctx)
{
    if (ctx == m_ctx)
        return;
    m_ctx = ctx;
    emit ctxChanged();
    updatePalette();
}

void SystemPalette::updatePalette()
{
    m_palettePriv.reset();
    switch(m_source)
    {
    case ColorSchemeModel::System:
        makeSystemPalette();
        break;
    case ColorSchemeModel::Day:
        makeLightPalette();
        break;
    case ColorSchemeModel::Night:
        makeDarkPalette();
        break;
    case ColorSchemeModel::Noir:
        makeNoirPalette();
        break;
    case ColorSchemeModel::Grindhouse:
        makeGrindhousePalette();
        break;
    case ColorSchemeModel::BubbleEra:
        makeBubbleEraPalette();
        break;
    default:
        break;
    }

    if (m_palettePriv)
    {
        connect(
            m_palettePriv.get(), &ExternalPaletteImpl::paletteChanged,
            this, &SystemPalette::updatePalette);
        connect(
            m_palettePriv.get(), &ExternalPaletteImpl::CSDMetricsChanged,
            this, &SystemPalette::CSDMetricsChanged);
    }
    emit paletteChanged();

    bool hasCSDImage = m_palettePriv && m_palettePriv->hasCSDImages();
    if (m_hasCSDImage != hasCSDImage)
    {
        m_hasCSDImage = hasCSDImage;
        emit hasCSDImageChanged();
    }
}

static quint64 makeKey(ColorContext::ColorSet colorSet, ColorContext::ColorSection section,
                       ColorContext::ColorName name, ColorContext::ColorState state)
{
    static_assert(VQTC_STATE_COUNT < (1<<4), "");
    static_assert(VQTC_SECTION_COUNT < (1<<4), "");
    static_assert(VQTC_NAME_COUNT < (1<<8), "");
    static_assert(VQTC_SET_COUNT < (1<<16), "");
    return  (colorSet << 16)
        + (name << 8)
        + (section << 4)
        + state;
}

void SystemPalette::setColor(ColorContext::ColorSet colorSet,  ColorContext::ColorSection section,
                             ColorContext::ColorName name, ColorContext::ColorState state, QColor color)
{

    quint64 key = makeKey(colorSet, section, name, state);
    m_colorMap[key] = color;
}


QColor SystemPalette::getColor(ColorContext::ColorSet colorSet, ColorContext::ColorSection section,
                               ColorContext::ColorName name, ColorContext::ColorState state) const
{
    typedef ColorContext C;

    quint64 key = makeKey(colorSet, section, name, state);
    auto it = m_colorMap.find(key);
    if (it != m_colorMap.cend())
        return *it;
    //we don't have the role explicitly set, fallback to the normal state
    key = makeKey(colorSet, section, name, C::Normal);
    it = m_colorMap.find(key);
    if (it != m_colorMap.cend())
        return *it;
    //we don't have the role explicitly set, fallback to the colorSet View
    //TODO do we want finer hierarchy?
    if (colorSet != C::View)
    {
        return getColor(C::View, section, name, state);
    }
    else
    {
        //nothing matches, that's probably an issue, return an ugly color
        return Qt::magenta;
    }
}


void SystemPalette::makeLightPalette()
{
    m_isDark = false;

    typedef ColorContext C;

    m_colorMap.clear();

    //base set
    {
        C::ColorSet CS = C::View;
        setColor(CS, C::Bg, C::Primary, C::Normal, lightGrey100 );
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::white );

        setColor(CS, C::Fg, C::Primary, C::Normal, darkGrey300);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.3));

        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::black, 0.7));

        setColor(CS, C::Bg, C::Negative, C::Normal, QColor("#fde7e9")); //FIXME
        setColor(CS, C::Fg, C::Negative, C::Normal, Qt::black); //FIXME

        setColor(CS, C::Bg, C::Neutral, C::Normal, QColor("#e4dab8")); //FIXME
        setColor(CS, C::Fg, C::Neutral, C::Normal, Qt::black); //FIXME

        setColor(CS, C::Bg, C::Positive, C::Normal, QColor("#dff6dd")); //FIXME
        setColor(CS, C::Fg, C::Positive, C::Normal, Qt::black); //FIXME

        setColor(CS, C::Decoration, C::VisualFocus, C::Normal, setColorAlpha(Qt::black, 0.0) );
        setColor(CS, C::Decoration, C::VisualFocus, C::Focused, Qt::black );

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, setColorAlpha(Qt::black, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::black, 0.0) );

        setColor(CS, C::Decoration, C::Separator, C::Normal, QColor("#E0E0E0")); //FIXME not a predef

        setColor(CS, C::Decoration, C::Shadow, C::Normal, setColorAlpha(Qt::black, 0.22));

        setColor(CS, C::Decoration, C::Accent, C::Normal, orange800);
        setColor(CS, C::Fg, C::Link, C::Normal, orange800 /* accent */);
    }

    //window banner & miniplayer
    {
        C::ColorSet CS = C::Window;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::white); //looks not white in figma more like #FDFDFD
        setColor(CS, C::Bg, C::Secondary, C::Normal, lightGrey400);
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor{"#E0E0E0"}); //FIXME not a predef
    }

    //badges
    {
        C::ColorSet CS = C::Badge;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
    }

    //tab button
    {
        C::ColorSet CS = C::TabButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(lightGrey300, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, lightGrey300);
        setColor(CS, C::Bg, C::Primary, C::Hovered, lightGrey300);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::black);
    }

    //tool button
    {
        C::ColorSet CS = C::ToolButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);
        setColor(CS, C::Bg, C::Secondary, C::Normal, lightGrey400);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::black);

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //menubar
    {
        C::ColorSet CS = C::MenuBar;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(lightGrey300, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, lightGrey300);
        setColor(CS, C::Bg, C::Primary, C::Hovered, lightGrey300);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));
    }

    //Item
    {
        C::ColorSet CS = C::Item;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(lightGrey600, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(lightGrey600, 0.5));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(lightGrey600, 0.5));

        setColor(CS, C::Bg, C::Highlight, C::Normal, lightGrey600);
        setColor(CS, C::Bg, C::Highlight, C::Focused, setColorAlpha(lightGrey600, 0.8));
        setColor(CS, C::Bg, C::Highlight, C::Hovered, setColorAlpha(lightGrey600, 0.8));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::black, 0.6));

        setColor(CS, C::Decoration, C::Indicator, C::Normal, QColor("#9e9e9e")); //FIXME not a predef

    }

    //Accent Buttons
    {
        C::ColorSet CS = C::ButtonAccent;
        setColor(CS, C::Bg, C::Primary, C::Normal, orange800);
        setColor(CS, C::Bg, C::Primary, C::Pressed, QColor("#e65609"));  //FIXME not a predef
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //Standard Buttons
    {
        C::ColorSet CS = C::ButtonStandard;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //tooltip
    {
        C::ColorSet CS = C::Tooltip;
        setColor(CS, C::Bg, C::Primary, C::Normal, lightGrey200);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
    }

    //slider
    {
        C::ColorSet CS = C::Slider;
        setColor(CS, C::Bg, C::Primary, C::Normal, lightGrey400); //#EEEEEE on the designs
        setColor(CS, C::Bg, C::Secondary, C::Normal, setColorAlpha("#lightGrey400", 0.2));
        setColor(CS, C::Fg, C::Primary, C::Normal, orange800);
        setColor(CS, C::Fg, C::Positive, C::Normal, "#0F7B0F");  //FIXME
        setColor(CS, C::Fg, C::Neutral, C::Normal, "#9D5D00");  //FIXME
        setColor(CS, C::Fg, C::Negative, C::Normal, "#C42B1C");  //FIXME
    }

    //Combo box
    {
        C::ColorSet CS = C::ComboBox;
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.8));
        setColor(CS, C::Bg, C::Secondary, C::Normal, lightGrey500);
    }

    //TextField
    {
        C::ColorSet CS = C::TextField;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange800);
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::black, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, darkGrey800); //FIXME
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white); //FIXME
    }

    //Switch
    {
        C::ColorSet CS = C::Switch;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.05));
        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.55));
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.55));

        setColor(CS, C::Bg, C::Secondary, C::Normal, orange800);
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //SpinBox
    {
        C::ColorSet CS = C::SpinBox;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange800);
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::black, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, darkGrey800); //FIXME
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white); //FIXME
    }
}

void SystemPalette::makeDarkPalette()
{
    m_isDark = true;

    m_colorMap.clear();

    typedef ColorContext C;

    {
        C::ColorSet CS = C::View;
        setColor(CS, C::Bg, C::Primary, C::Normal, darkGrey300 );
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black );

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white );
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3) );

        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::white, 0.6));

        // Improved dark mode colors for better contrast and visibility
        setColor(CS, C::Bg, C::Negative, C::Normal, QColor("#D85566")); // Better contrast for dark mode
        setColor(CS, C::Fg, C::Negative, C::Normal, Qt::white);

        setColor(CS, C::Bg, C::Neutral, C::Normal, QColor("#FFD700")); // Brighter yellow for dark mode
        setColor(CS, C::Fg, C::Neutral, C::Normal, Qt::black);

        setColor(CS, C::Bg, C::Positive, C::Normal, QColor("#4CAF50")); // Better green for dark mode
        setColor(CS, C::Fg, C::Positive, C::Normal, Qt::white);

        setColor(CS, C::Decoration, C::VisualFocus, C::Normal, setColorAlpha(Qt::white, 0.0) );
        setColor(CS, C::Decoration, C::VisualFocus, C::Focused, Qt::white );

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, setColorAlpha(Qt::white, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Decoration, C::Shadow, C::Normal, setColorAlpha(Qt::black, 0.22));

        setColor(CS, C::Decoration, C::Separator, C::Normal, darkGrey800);

        setColor(CS, C::Decoration, C::Accent, C::Normal, orange500);
        setColor(CS, C::Fg, C::Link, C::Normal, orange500);
    }

    //window banner & miniplayer
    {
        C::ColorSet CS = C::Window;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::black); //FIXME
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black);
        setColor(CS, C::Decoration, C::Border, C::Normal, darkGrey800); //FIXME not a predef
    }

    //badges
    {
        C::ColorSet CS = C::Badge;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.8));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
    }

    //tab button
    {
        C::ColorSet CS = C::TabButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(darkGrey800, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, darkGrey800);
        setColor(CS, C::Bg, C::Primary, C::Hovered, darkGrey800);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //tool button
    {
        C::ColorSet CS = C::ToolButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //menubar
    {
        C::ColorSet CS = C::MenuBar;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(darkGrey800, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, darkGrey800);
        setColor(CS, C::Bg, C::Primary, C::Hovered, darkGrey800);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
    }

    //Item
    {
        C::ColorSet CS = C::Item;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(darkGrey800, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(darkGrey800, 0.5));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(darkGrey800, 0.5));

        setColor(CS, C::Bg, C::Highlight, C::Normal, darkGrey800);
        setColor(CS, C::Bg, C::Highlight, C::Focused, setColorAlpha(darkGrey800, 0.8));
        setColor(CS, C::Bg, C::Highlight, C::Hovered, setColorAlpha(darkGrey800, 0.8));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::white, 0.6));

        setColor(CS, C::Decoration, C::Indicator, C::Normal, QColor("#666666"));  //FIXME not a predef
    }

    //Accent Buttons
    {
        C::ColorSet CS = C::ButtonAccent;
        setColor(CS, C::Bg, C::Primary, C::Normal, orange500);
        setColor(CS, C::Bg, C::Primary, C::Pressed, QColor("#FF7D00"));  // Better pressed state for dark mode
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //Standard Buttons
    {
        C::ColorSet CS = C::ButtonStandard;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //tooltip
    {
        C::ColorSet CS = C::Tooltip;
        setColor(CS, C::Bg, C::Primary, C::Normal, darkGrey200);  // Darker tooltip background for AMOLED
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
    }

    //slider
    {
        C::ColorSet CS = C::Slider;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha("#929292", 0.2)); //FIXME not in the palette
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha("#929292", 0.4));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha("#929292", 0.4));

        setColor(CS, C::Fg, C::Primary, C::Normal, orange500);
        setColor(CS, C::Fg, C::Positive, C::Normal, "#0F7B0F");  //FIXME
        setColor(CS, C::Fg, C::Neutral, C::Normal, "#9D5D00");  //FIXME
        setColor(CS, C::Fg, C::Negative, C::Normal, "#C42B1C");  //FIXME
    }

    //Combo box
    {
        C::ColorSet CS = C::ComboBox;
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(darkGrey300, 0.8));
        setColor(CS, C::Bg, C::Secondary, C::Normal, darkGrey500);
    }

    //TextField
    {
        C::ColorSet CS = C::TextField;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange500 );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, lightGrey600); //FIXME
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black); //FIXME
    }

    //Switch
    {
        C::ColorSet CS = C::Switch;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.05));
        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.55));
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.55));
        setColor(CS, C::Bg, C::Secondary, C::Normal, orange500);
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::black);
    }

    //Spinbox
    {
        C::ColorSet CS = C::SpinBox;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange500 );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, lightGrey600); //FIXME
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black); //FIXME
    }
}

void SystemPalette::makeNoirPalette()
{
    m_isDark = true;

    m_colorMap.clear();

    typedef ColorContext C;

    // Noir style: Pure black and white palette with grayscale accents
    // No colors, only shades of gray for a classic noir/film noir aesthetic

    {
        C::ColorSet CS = C::View;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#1a1a1a") ); // Very dark gray
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black );

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white );
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3) );

        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::white, 0.6));

        // All accent colors are grayscale in noir mode
        setColor(CS, C::Bg, C::Negative, C::Normal, QColor("#333333")); // Dark gray
        setColor(CS, C::Fg, C::Negative, C::Normal, Qt::white);

        setColor(CS, C::Bg, C::Neutral, C::Normal, QColor("#555555")); // Medium gray
        setColor(CS, C::Fg, C::Neutral, C::Normal, Qt::white);

        setColor(CS, C::Bg, C::Positive, C::Normal, QColor("#444444")); // Dark-medium gray
        setColor(CS, C::Fg, C::Positive, C::Normal, Qt::white);

        setColor(CS, C::Decoration, C::VisualFocus, C::Normal, setColorAlpha(Qt::white, 0.0) );
        setColor(CS, C::Decoration, C::VisualFocus, C::Focused, Qt::white );

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.3) );
        setColor(CS, C::Decoration, C::Border, C::Focused, setColorAlpha(Qt::white, 0.6) );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.6) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Decoration, C::Shadow, C::Normal, setColorAlpha(Qt::black, 0.4));

        setColor(CS, C::Decoration, C::Separator, C::Normal, QColor("#2a2a2a"));

        setColor(CS, C::Decoration, C::Accent, C::Normal, QColor("#888888")); // Lighter gray
        setColor(CS, C::Fg, C::Link, C::Normal, QColor("#999999")); // Even lighter gray
    }

    //window banner & miniplayer
    {
        C::ColorSet CS = C::Window;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black);
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#2a2a2a"));
    }

    //badges
    {
        C::ColorSet CS = C::Badge;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.9));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
    }

    //tab button
    {
        C::ColorSet CS = C::TabButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#2a2a2a"), 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, QColor("#2a2a2a"));
        setColor(CS, C::Bg, C::Primary, C::Hovered, QColor("#2a2a2a"));

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.5));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //tool button
    {
        C::ColorSet CS = C::ToolButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.5));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //menubar
    {
        C::ColorSet CS = C::MenuBar;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#2a2a2a"), 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, QColor("#2a2a2a"));
        setColor(CS, C::Bg, C::Primary, C::Hovered, QColor("#2a2a2a"));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
    }

    //Item
    {
        C::ColorSet CS = C::Item;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#2a2a2a"), 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(QColor("#2a2a2a"), 0.5));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(QColor("#2a2a2a"), 0.5));

        setColor(CS, C::Bg, C::Highlight, C::Normal, QColor("#3a3a3a"));
        setColor(CS, C::Bg, C::Highlight, C::Focused, setColorAlpha(QColor("#3a3a3a"), 0.8));
        setColor(CS, C::Bg, C::Highlight, C::Hovered, setColorAlpha(QColor("#3a3a3a"), 0.8));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::white, 0.5));

        setColor(CS, C::Decoration, C::Indicator, C::Normal, QColor("#555555"));
    }

    //Accent Buttons (grayscale)
    {
        C::ColorSet CS = C::ButtonAccent;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#555555"));
        setColor(CS, C::Bg, C::Primary, C::Pressed, QColor("#444444"));
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.15));

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //Standard Buttons
    {
        C::ColorSet CS = C::ButtonStandard;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.5));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //tooltip
    {
        C::ColorSet CS = C::Tooltip;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
    }

    //slider
    {
        C::ColorSet CS = C::Slider;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.15));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(Qt::white, 0.25));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(Qt::white, 0.25));

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#777777"));
        setColor(CS, C::Fg, C::Positive, C::Normal, QColor("#666666"));
        setColor(CS, C::Fg, C::Neutral, C::Normal, QColor("#777777"));
        setColor(CS, C::Fg, C::Negative, C::Normal, QColor("#666666"));
    }

    //Combo box
    {
        C::ColorSet CS = C::ComboBox;
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#1a1a1a"), 0.8));
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#2a2a2a"));
    }

    //TextField
    {
        C::ColorSet CS = C::TextField;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.3) );
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#777777") );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.5) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, QColor("#3a3a3a"));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);
    }

    //Switch
    {
        C::ColorSet CS = C::Switch;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.05));
        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.45));
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.45));
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#555555"));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //Spinbox
    {
        C::ColorSet CS = C::SpinBox;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.3) );
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#777777") );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.5) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, QColor("#3a3a3a"));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);
    }
}

void SystemPalette::makeGrindhousePalette()
{
    m_isDark = true;

    m_colorMap.clear();

    typedef ColorContext C;

    // Grindhouse style: Oversaturated, high-contrast exploitation film aesthetic
    // Bold reds, magentas, yellows with gritty appearance

    {
        C::ColorSet CS = C::View;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#0a0a0a") ); // Deep black
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black );

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white );
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3) );

        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::white, 0.7));

        // Grindhouse: Bold, oversaturated colors
        setColor(CS, C::Bg, C::Negative, C::Normal, QColor("#DD0000")); // Saturated red
        setColor(CS, C::Fg, C::Negative, C::Normal, Qt::white);

        setColor(CS, C::Bg, C::Neutral, C::Normal, QColor("#FFDD00")); // Bright yellow
        setColor(CS, C::Fg, C::Neutral, C::Normal, Qt::black);

        setColor(CS, C::Bg, C::Positive, C::Normal, QColor("#00DD00")); // Bright green
        setColor(CS, C::Fg, C::Positive, C::Normal, Qt::black);

        setColor(CS, C::Decoration, C::VisualFocus, C::Normal, setColorAlpha(Qt::white, 0.0) );
        setColor(CS, C::Decoration, C::VisualFocus, C::Focused, QColor("#FF00FF") ); // Magenta focus

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#FF00FF") ); // Magenta highlight
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#FF6600") ); // Orange hover
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.1) );

        setColor(CS, C::Decoration, C::Shadow, C::Normal, setColorAlpha(Qt::black, 0.5));

        setColor(CS, C::Decoration, C::Separator, C::Normal, QColor("#333333"));

        setColor(CS, C::Decoration, C::Accent, C::Normal, QColor("#FF0099")); // Hot pink
        setColor(CS, C::Fg, C::Link, C::Normal, QColor("#00FFFF")); // Cyan links
    }

    //window banner & miniplayer
    {
        C::ColorSet CS = C::Window;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#1a1a1a"));
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#FF0099")); // Hot pink border
    }

    //badges
    {
        C::ColorSet CS = C::Badge;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#FFDD00"));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
    }

    //tab button
    {
        C::ColorSet CS = C::TabButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#FF0099"), 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, QColor("#FF0099"));
        setColor(CS, C::Bg, C::Primary, C::Hovered, QColor("#DD0000"));

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //tool button
    {
        C::ColorSet CS = C::ToolButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#FF00FF")); // Magenta
        setColor(CS, C::Fg, C::Primary, C::Focused, QColor("#FFDD00")); // Yellow focus
        setColor(CS, C::Fg, C::Primary, C::Hovered, QColor("#00FFFF")); // Cyan hover
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#FF00FF")); // Magenta
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#FF6600")); // Orange
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //menubar
    {
        C::ColorSet CS = C::MenuBar;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#FF0099"), 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, QColor("#FF0099"));
        setColor(CS, C::Bg, C::Primary, C::Hovered, QColor("#DD0000"));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
    }

    //Item
    {
        C::ColorSet CS = C::Item;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#FF0099"), 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(QColor("#FF0099"), 0.5));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(QColor("#DD0000"), 0.5));

        setColor(CS, C::Bg, C::Highlight, C::Normal, QColor("#FF0099"));
        setColor(CS, C::Bg, C::Highlight, C::Focused, QColor("#00FFFF"));
        setColor(CS, C::Bg, C::Highlight, C::Hovered, QColor("#FFDD00"));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Secondary, C::Normal, QColor("#FFDD00")); // Yellow secondary

        setColor(CS, C::Decoration, C::Indicator, C::Normal, QColor("#00FFFF")); // Cyan indicator
    }

    //Accent Buttons (grindhouse red/magenta)
    {
        C::ColorSet CS = C::ButtonAccent;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#DD0000"));
        setColor(CS, C::Bg, C::Primary, C::Pressed, QColor("#FF00FF"));
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.15));

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#FFDD00")); // Yellow border
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#00FFFF")); // Cyan focus
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#FF6600")); // Orange hover
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //Standard Buttons
    {
        C::ColorSet CS = C::ButtonStandard;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#FF00FF")); // Magenta
        setColor(CS, C::Fg, C::Primary, C::Focused, QColor("#00FFFF")); // Cyan
        setColor(CS, C::Fg, C::Primary, C::Hovered, QColor("#FFDD00")); // Yellow
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#00FFFF")); // Cyan
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#FFDD00")); // Yellow
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //tooltip
    {
        C::ColorSet CS = C::Tooltip;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#1a1a1a"));
        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#FFDD00")); // Yellow text
    }

    //slider
    {
        C::ColorSet CS = C::Slider;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#FF0099"), 0.2));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(QColor("#FF0099"), 0.4));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(QColor("#FF6600"), 0.4));

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#FFDD00"));
        setColor(CS, C::Fg, C::Positive, C::Normal, QColor("#00DD00"));
        setColor(CS, C::Fg, C::Neutral, C::Normal, QColor("#FF6600"));
        setColor(CS, C::Fg, C::Negative, C::Normal, QColor("#DD0000"));
    }

    //Combo box
    {
        C::ColorSet CS = C::ComboBox;
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#0a0a0a"), 0.9));
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#1a1a1a"));
    }

    //TextField
    {
        C::ColorSet CS = C::TextField;
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#FF00FF") );
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#FFDD00") );
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#00FFFF") );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.1) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, QColor("#1a1a1a"));
        setColor(CS, C::Fg, C::Highlight, C::Normal, QColor("#FFDD00"));
    }

    //Switch
    {
        C::ColorSet CS = C::Switch;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.05));
        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#FF00FF")); // Magenta
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#FF00FF")); // Magenta
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#DD0000"));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //Spinbox
    {
        C::ColorSet CS = C::SpinBox;
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#FF00FF") );
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#FFDD00") );
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#00FFFF") );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.1) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, QColor("#1a1a1a"));
        setColor(CS, C::Fg, C::Highlight, C::Normal, QColor("#FFDD00"));
    }
}

void SystemPalette::makeBubbleEraPalette()
{
    m_isDark = false;

    m_colorMap.clear();

    typedef ColorContext C;

    // Bubble-Era style: Late-90s pastel aesthetic with heavy diffusion/bloom
    // Soft pinks, whites, greens with washed-out, dreamy appearance

    {
        C::ColorSet CS = C::View;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#F5F5FF") ); // Very light lavender
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#FFFFFF") ); // Pure white

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#666666") ); // Soft gray text
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(QColor("#999999"), 0.5) );

        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(QColor("#888888"), 0.8));

        // Bubble-Era: Soft pastel colors
        setColor(CS, C::Bg, C::Negative, C::Normal, QColor("#FFB3D9")); // Pastel pink
        setColor(CS, C::Fg, C::Negative, C::Normal, QColor("#666666"));

        setColor(CS, C::Bg, C::Neutral, C::Normal, QColor("#E5FFE5")); // Pastel green
        setColor(CS, C::Fg, C::Neutral, C::Normal, QColor("#666666"));

        setColor(CS, C::Bg, C::Positive, C::Normal, QColor("#B3E5FF")); // Pastel blue
        setColor(CS, C::Fg, C::Positive, C::Normal, QColor("#666666"));

        setColor(CS, C::Decoration, C::VisualFocus, C::Normal, setColorAlpha(Qt::white, 0.0) );
        setColor(CS, C::Decoration, C::VisualFocus, C::Focused, QColor("#FFD9E5") ); // Soft pink focus

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(QColor("#FFCCDD"), 0.6) ); // Pastel pink border
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#FFA3D5") ); // Medium pastel pink
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#B3E5FF") ); // Pastel blue hover
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.3) );

        setColor(CS, C::Decoration, C::Shadow, C::Normal, setColorAlpha(QColor("#FFCCDD"), 0.2)); // Soft shadow

        setColor(CS, C::Decoration, C::Separator, C::Normal, QColor("#E5D9FF")); // Light lavender separator

        setColor(CS, C::Decoration, C::Accent, C::Normal, QColor("#FFB3D9")); // Soft pink accent
        setColor(CS, C::Fg, C::Link, C::Normal, QColor("#B3D9FF")); // Pastel blue links
    }

    //window banner & miniplayer
    {
        C::ColorSet CS = C::Window;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#F5F5FF")); // Light lavender
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#FFFFFF")); // White
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#FFCCDD")); // Pastel pink border
    }

    //badges
    {
        C::ColorSet CS = C::Badge;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#FFB3D9")); // Pastel pink
        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#666666")); // Dark gray text
    }

    //tab button
    {
        C::ColorSet CS = C::TabButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#FFD9E5"), 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, QColor("#FFD9E5")); // Light pastel pink
        setColor(CS, C::Bg, C::Primary, C::Hovered, QColor("#FFE5EC")); // Even lighter pastel

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(QColor("#666666"), 0.7));
        setColor(CS, C::Fg, C::Primary, C::Focused, QColor("#666666"));
        setColor(CS, C::Fg, C::Primary, C::Hovered, QColor("#666666"));
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(QColor("#999999"), 0.4));
        setColor(CS, C::Fg, C::Secondary, C::Normal, QColor("#666666"));
    }

    //tool button
    {
        C::ColorSet CS = C::ToolButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#FFFFFF"));

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#9999CC")); // Soft lavender
        setColor(CS, C::Fg, C::Primary, C::Focused, QColor("#FFB3D9")); // Pastel pink focus
        setColor(CS, C::Fg, C::Primary, C::Hovered, QColor("#B3E5FF")); // Pastel blue hover
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(QColor("#CCCCCC"), 0.5));
        setColor(CS, C::Fg, C::Secondary, C::Normal, QColor("#666666"));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#FFB3D9")); // Pastel pink
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#E5D9FF")); // Lavender
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //menubar
    {
        C::ColorSet CS = C::MenuBar;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#FFE5EC"), 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, QColor("#FFE5EC")); // Very light pastel pink
        setColor(CS, C::Bg, C::Primary, C::Hovered, QColor("#FFD9E5")); // Light pastel pink
        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#666666"));
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(QColor("#999999"), 0.4));
    }

    //Item
    {
        C::ColorSet CS = C::Item;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#FFE5EC"), 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(QColor("#FFE5EC"), 0.6)); // Light pastel pink
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(QColor("#B3E5FF"), 0.6)); // Pastel blue

        setColor(CS, C::Bg, C::Highlight, C::Normal, QColor("#FFB3D9")); // Pastel pink
        setColor(CS, C::Bg, C::Highlight, C::Focused, QColor("#B3E5FF")); // Pastel blue
        setColor(CS, C::Bg, C::Highlight, C::Hovered, QColor("#E5FFE5")); // Pastel green
        setColor(CS, C::Fg, C::Highlight, C::Normal, QColor("#666666"));

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#666666"));
        setColor(CS, C::Fg, C::Secondary, C::Normal, QColor("#9999CC")); // Soft lavender secondary

        setColor(CS, C::Decoration, C::Indicator, C::Normal, QColor("#B3E5FF")); // Pastel blue indicator
    }

    //Accent Buttons (pastel pink)
    {
        C::ColorSet CS = C::ButtonAccent;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#FFB3D9")); // Pastel pink
        setColor(CS, C::Bg, C::Primary, C::Pressed, QColor("#FFA3D5")); // Medium pastel pink
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(QColor("#CCCCCC"), 0.3));

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#666666"));
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(QColor("#999999"), 0.4));

        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#E5D9FF")); // Lavender border
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#B3E5FF")); // Pastel blue focus
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#E5FFE5")); // Pastel green hover
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //Standard Buttons
    {
        C::ColorSet CS = C::ButtonStandard;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#9999CC")); // Soft lavender
        setColor(CS, C::Fg, C::Primary, C::Focused, QColor("#FFB3D9")); // Pastel pink
        setColor(CS, C::Fg, C::Primary, C::Hovered, QColor("#B3E5FF")); // Pastel blue
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(QColor("#CCCCCC"), 0.5));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#B3E5FF")); // Pastel blue
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#E5D9FF")); // Lavender
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //tooltip
    {
        C::ColorSet CS = C::Tooltip;
        setColor(CS, C::Bg, C::Primary, C::Normal, QColor("#FFFAFF")); // Very light pink
        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#666666")); // Dark gray text
    }

    //slider
    {
        C::ColorSet CS = C::Slider;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#FFB3D9"), 0.2)); // Light pastel pink
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(QColor("#FFB3D9"), 0.3));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(QColor("#B3E5FF"), 0.3)); // Pastel blue

        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#FFB3D9")); // Pastel pink
        setColor(CS, C::Fg, C::Positive, C::Normal, QColor("#B3E5FF")); // Pastel blue
        setColor(CS, C::Fg, C::Neutral, C::Normal, QColor("#E5FFE5")); // Pastel green
        setColor(CS, C::Fg, C::Negative, C::Normal, QColor("#FFE5CC")); // Pastel peach
    }

    //Combo box
    {
        C::ColorSet CS = C::ComboBox;
        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#666666"));
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#F5F5FF"), 0.9)); // Light lavender
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#FFFFFF"));
    }

    //TextField
    {
        C::ColorSet CS = C::TextField;
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#FFB3D9") );
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#B3E5FF") );
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#E5D9FF") );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(QColor("#CCCCCC"), 0.3) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, QColor("#FFFFFF"));
        setColor(CS, C::Fg, C::Highlight, C::Normal, QColor("#FFB3D9"));
    }

    //Switch
    {
        C::ColorSet CS = C::Switch;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(QColor("#FFB3D9"), 0.15));
        setColor(CS, C::Fg, C::Primary, C::Normal, QColor("#9999CC")); // Soft lavender
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#FFB3D9")); // Pastel pink
        setColor(CS, C::Bg, C::Secondary, C::Normal, QColor("#B3E5FF")); // Pastel blue
        setColor(CS, C::Fg, C::Secondary, C::Normal, QColor("#666666"));
    }

    //Spinbox
    {
        C::ColorSet CS = C::SpinBox;
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor("#FFB3D9") );
        setColor(CS, C::Decoration, C::Border, C::Focused, QColor("#B3E5FF") );
        setColor(CS, C::Decoration, C::Border, C::Hovered, QColor("#E5D9FF") );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(QColor("#CCCCCC"), 0.3) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, QColor("#FFFFFF"));
        setColor(CS, C::Fg, C::Highlight, C::Normal, QColor("#FFB3D9"));
    }
}

void SystemPalette::makeSystemPalette()
{
    if (!m_ctx)
    {
        //can't initialise system palette, fallback to default
        makeLightPalette();
        return;
    }

    auto palette = std::make_unique<ExternalPaletteImpl>(m_ctx, *this);
    if (!palette->init())
    {
        //can't initialise system palette, fallback to default
        makeLightPalette();
        return;
    }

    m_colorMap.clear();
    int ret = palette->update();
    if (ret != VLC_SUCCESS)
    {
        if (palette->isThemeDark())
            makeDarkPalette();
        else
            makeLightPalette();
    }
    else
    {
        m_isDark = palette->isThemeDark();
    }

    m_palettePriv = std::move(palette);
}
