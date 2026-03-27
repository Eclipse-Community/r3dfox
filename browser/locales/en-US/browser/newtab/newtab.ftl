# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = New Tab
#  (developer note): @nova-cleanup(remove-string): Remove newtab-customize-panel-icon-button once Nova lands, will be using newtab-customize-panel-label instead
newtab-customize-panel-icon-button =
    .title = Customize this page
#  (developer note): @nova-cleanup(remove-string): Remove newtab-customize-panel-icon-button-label once Nova lands, will be using newtab-customize-panel-label instead
newtab-customize-panel-icon-button-label = Customize
newtab-customize-panel-label =
    .label = Customize
newtab-settings-dialog-label =
    .aria-label = Settings
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Search
    .aria-label = Search

# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Search with { $engine } or enter address
newtab-search-box-handoff-text-no-engine = Search or enter address
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Search with { $engine } or enter address
    .title = Search with { $engine } or enter address
    .aria-label = Search with { $engine } or enter address
newtab-search-box-handoff-input-no-engine =
    .placeholder = Search or enter address
    .title = Search or enter address
    .aria-label = Search or enter address

newtab-search-box-text = Search the web
newtab-search-box-input =
    .placeholder = Search the web
    .aria-label = Search the web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Add Search Engine
newtab-topsites-add-shortcut-header = New Shortcut
newtab-topsites-edit-shortcut-header = Edit Shortcut
newtab-topsites-add-shortcut-label = Add Shortcut
newtab-topsites-add-shortcut-title =
    .title = Add Shortcut
    .aria-label = Add Shortcut
newtab-topsites-title-label = Title
newtab-topsites-title-input =
    .placeholder = Enter a title

newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Type or paste a URL
newtab-topsites-url-validation = Valid URL required

## Clear text button for the URL and image URL input fields in the Top Sites form.

newtab-topsites-clear-input =
    .aria-label = Clear text

newtab-topsites-image-url-label = Custom Image URL
newtab-topsites-use-image-link = Use a custom image…
newtab-topsites-image-validation = Image failed to load. Try a different URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cancel
newtab-topsites-delete-history-button = Delete from History
newtab-topsites-save-button = Save
newtab-topsites-preview-button = Preview
newtab-topsites-add-button = Add

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Are you sure you want to delete every instance of this page from your history?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = This action cannot be undone.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsored

## Label used by screen readers for pinned top sites

# Variables:
#   $title (string) - The label or hostname of the site.
topsite-label-pinned =
    .aria-label = { $title } (pinned)
    .title = { $title }

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Open menu
    .aria-label = Open menu

# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Open menu
    .aria-label = Open context menu for { $title }

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Edit
newtab-menu-open-new-window = Open in a New Window
newtab-menu-open-new-private-window = Open in a New Private Window
newtab-menu-dismiss = Dismiss
newtab-menu-pin = Pin
newtab-menu-unpin = Unpin
newtab-menu-delete-history = Delete from History
newtab-menu-show-privacy-info = Our sponsors & your privacy
# Report is a verb (i.e. report issue with the content).
newtab-menu-report = Report
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Block
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow-topic = Unfollow
# Context menu option to open a support page explaining the New Tab personalization features and privacy controls.
newtab-menu-section-learn-more = Learn more

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Manage sponsored content
newtab-menu-our-sponsors-and-your-privacy = Our sponsors and your privacy
newtab-menu-report-this-ad = Report this ad

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Remove Bookmark
# Bookmark is a verb here.
newtab-menu-bookmark = Bookmark

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copy Download Link
newtab-menu-go-to-download-page = Go to Download Page
newtab-menu-remove-download = Remove from History

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Show in Finder
       *[other] Open Containing Folder
    }
newtab-menu-open-file = Open File

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visited
newtab-label-bookmarked = Bookmarked
newtab-label-removed-bookmark = Bookmark removed
newtab-label-recommended = Trending
newtab-label-saved = Saved to { -pocket-brand-name }
newtab-label-download = Downloaded

# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsored

# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsored by { $sponsor }

# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

# This string is used under fixed size ads to indicate sponsored content
newtab-label-sponsored-fixed = Sponsored

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-privacy-notice = Privacy Notice

## Section Headers.

newtab-section-header-topsites = Top Sites
newtab-section-header-recent-activity = Recent activity
newtab-section-header-stories = Thought-provoking stories
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Today’s picks for you

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Start browsing, and we’ll show some of the great articles, videos, and other pages you’ve recently visited or bookmarked here.

# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = You’ve caught up. Check back later for more stories. Can’t wait? Select a popular topic to find more great stories from around the web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = You are caught up!
newtab-discovery-empty-section-topstories-content = Check back later for more stories.
newtab-discovery-empty-section-topstories-try-again-button = Try Again
newtab-discovery-empty-section-topstories-loading = Loading…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Oops! We almost loaded this section, but not quite.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oops, something went wrong loading this content.
newtab-error-fallback-refresh-link = Refresh page to try again.

## Customization Menu

#  (developer note): @nova-cleanup(remove-string): Remove old string once Nova lands. The newtab-custom-shortcuts-nova string will take over
newtab-custom-shortcuts-toggle =
  .label = Shortcuts
  .description = Sites you save or visit

newtab-custom-shortcuts-nova =
  .label = Shortcuts

newtab-custom-row-description =
  .description = Number of rows
# Variables
#   $num (number) - Number of rows to display
#  (developer note): @nova-cleanup(remove-string): Remove string once Nova lands. We won't be using "row"/"rows" anymore for the dropdown
newtab-custom-row-selector2 =
        .label = { $num ->
            [one] { $num } row
           *[other] { $num } rows
        }
#  (developer note): @nova-cleanup(remove-string): Remove string once Nova lands. We won't be having a description under "Recommended stories" anymore
newtab-custom-stories-toggle =
  .label = Recommended stories
  .description = Exceptional content curated by Mozilla
newtab-recommended-stories-toggle =
  .label = Recommended stories
newtab-custom-stories-personalized-toggle =
  .label = Stories
newtab-custom-stories-personalized-checkbox-label = Personalized stories based on your activity
newtab-custom-weather-toggle =
  .label = Weather
  .description = Today’s forecast at a glance
newtab-custom-widget-weather-toggle =
  .label = Weather
newtab-custom-widget-lists-toggle =
  .label = Lists
newtab-custom-widget-timer-toggle =
  .label = Timer
newtab-custom-widget-clock-toggle =
  .label = Clock
newtab-custom-widget-sports-toggle2 =
  .label = Sports
newtab-custom-widget-section-title = Widgets
newtab-custom-widget-section-toggle =
    .label = Widgets
newtab-widget-manage-title = Widgets
newtab-widget-manage-widget-button =
    .label = Manage widgets

# Tooltip for close button
newtab-custom-close-menu-button =
    .title = Close
    .aria-label = Close menu
newtab-custom-settings = Manage more settings

## New Tab Wallpapers

newtab-wallpaper-title = Wallpapers
newtab-wallpaper-reset = Reset to default
#  (developer note): @nova-cleanup(remove-string): Remove old "Upload an image" string once Nova lands. The new "Add an image"  string will take over
newtab-wallpaper-upload-image = Upload an image
newtab-wallpaper-add-an-image = Add an image
newtab-wallpaper-custom-color = Choose a color
newtab-wallpaper-toggle-title =
    .label = Wallpapers
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = The image exceeded the file size limit of { $file_size }MB. Please try uploading a smaller file.
newtab-wallpaper-error-upload-file-type = We couldn’t upload your file. Please try again with an image file.
newtab-wallpaper-light-red-panda = Red panda
newtab-wallpaper-light-mountain = White mountain
newtab-wallpaper-light-sky = Sky with purple and pink clouds
newtab-wallpaper-light-color = Blue, pink and yellow shapes
newtab-wallpaper-light-landscape = Blue mist mountain landscape
newtab-wallpaper-light-beach = Beach with palm tree
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Red and blue shapes
newtab-wallpaper-dark-panda = Red panda hidden in forest
newtab-wallpaper-dark-sky = City landscape with a night sky
newtab-wallpaper-dark-mountain = Landscape mountain
newtab-wallpaper-dark-city = Purple city landscape
newtab-wallpaper-dark-fox-anniversary = A fox on the pavement near a forest
newtab-wallpaper-light-fox-anniversary = A fox in a grassy field with a misty mountain landscape

## Solid Colors

#  (developer note): @nova-cleanup(remove-string): Remove old "Solid colors" string once Nova lands. The simplified "Colors" string will take over
newtab-wallpaper-category-title-colors = Solid colors
newtab-wallpaper-colors = Colors
newtab-wallpaper-blue = Blue
newtab-wallpaper-light-blue = Light blue
newtab-wallpaper-light-purple = Light purple
newtab-wallpaper-light-green = Light green
newtab-wallpaper-green = Green
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Yellow
newtab-wallpaper-orange = Orange
newtab-wallpaper-pink = Pink
newtab-wallpaper-light-pink = Light pink
newtab-wallpaper-red = Red
newtab-wallpaper-dark-blue = Dark blue
newtab-wallpaper-dark-purple = Dark purple
newtab-wallpaper-dark-green = Dark green
newtab-wallpaper-brown = Brown

## Abstract

newtab-wallpaper-category-title-abstract = Abstract
newtab-wallpaper-abstract-green = Green shapes
newtab-wallpaper-abstract-blue = Blue shapes
newtab-wallpaper-abstract-purple = Purple shapes
newtab-wallpaper-abstract-orange = Orange shapes
newtab-wallpaper-gradient-orange = Gradient orange and pink
newtab-wallpaper-abstract-blue-purple = Blue and purple shapes
newtab-wallpaper-abstract-white-curves = White with shaded curves
newtab-wallpaper-abstract-purple-green = Purple and green light gradient
newtab-wallpaper-abstract-blue-purple-waves = Blue and purple wavy shapes
newtab-wallpaper-abstract-black-waves = Black wavy shapes

## Photographs

newtab-wallpaper-category-title-photographs = Photographs
newtab-wallpaper-beach-at-sunrise = Beach at sunrise
newtab-wallpaper-beach-at-sunset = Beach at sunset
newtab-wallpaper-storm-sky = Storm sky
newtab-wallpaper-sky-with-pink-clouds = Sky with pink clouds
newtab-wallpaper-red-panda-yawns-in-a-tree = Red panda yawns in a tree
newtab-wallpaper-white-mountains = White mountains
newtab-wallpaper-hot-air-balloons = Assorted color of hot air balloons during daytime
newtab-wallpaper-starry-canyon = Blue starry night
newtab-wallpaper-suspension-bridge = Grey full-suspension bridge photography during daytime
newtab-wallpaper-sand-dunes = White sand dunes
newtab-wallpaper-palm-trees = Silhouette of coconut palm trees during golden hour
newtab-wallpaper-blue-flowers = Closeup photography of blue-petaled flowers in bloom

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Celestial
newtab-wallpaper-celestial-lunar-eclipse = Lunar eclipse
newtab-wallpaper-celestial-earth-night = Night photo from low Earth orbit
newtab-wallpaper-celestial-starry-sky = Starry sky
newtab-wallpaper-celestial-eclipse-time-lapse = Lunar eclipse time lapse
newtab-wallpaper-celestial-black-hole = Black hole galaxy illustration
newtab-wallpaper-celestial-river = Satellite image of river

## Firefox

newtab-wallpaper-category-title-firefox = { -brand-product-name }


# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Photo by <a data-l10n-name="name-link">{ $author_string }</a> on <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Try a splash of color
newtab-wallpaper-feature-highlight-content = Give your New Tab a fresh look with wallpapers.
newtab-wallpaper-feature-highlight-button = Got it
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Dismiss
    .aria-label = Close popup
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast-description =
    .title = See forecast in { $provider }
    .aria-description = { $provider } ∙ Sponsored
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsored
newtab-weather-menu-change-location = Change location
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Search location
    .aria-label = Search location
# "Current" refers to the user's physical/geographic location detected via geolocation.
newtab-weather-change-location-search-use-current =
    .label = Use current location
newtab-weather-menu-weather-display = Weather display
newtab-weather-todays-forecast = Today’s forecast
newtab-weather-see-full-forecast = See full forecast
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simple
newtab-weather-menu-change-weather-display-simple = Switch to simple view
newtab-weather-menu-weather-display-option-detailed = Detailed
newtab-weather-menu-change-weather-display-detailed = Switch to detailed view
newtab-weather-menu-temperature-units = Temperature units
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Switch to Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Switch to Celsius
newtab-weather-menu-learn-more = Learn more
newtab-weather-menu-detect-my-location = Detect my location
# This message is shown if user is working offline
newtab-weather-error-not-available = Weather data is not available right now.
newtab-weather-opt-in-see-weather = Do you want to see the weather for your location?
newtab-weather-opt-in-not-now =
    .label = Not now
newtab-weather-opt-in-yes =
    .label = Yes
newtab-weather-opt-in-headline = Get your local weather forecast
newtab-weather-opt-in-use-location =
    .label = Use location
newtab-weather-opt-in-choose-location = Choose location
# We'll be showing static (fake) weather data if the user has not opted in to using their location
newtab-weather-static-city = New York City
# "Highest" here refers to the highest temperature of the day
newtab-weather-high =
    .aria-label = High
# "Lowest" here refers to the lowest temperature of the day
newtab-weather-low =
    .aria-label = Low

## Topic Labels

newtab-topic-label-business = Business
newtab-topic-label-career = Career
newtab-topic-label-education = Education
newtab-topic-label-arts = Entertainment
newtab-topic-label-food = Food
newtab-topic-label-health = Health
newtab-topic-label-hobbies = Gaming
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Money
newtab-topic-label-society-parenting = Parenting
newtab-topic-label-government = Politics
newtab-topic-label-education-science = Science
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Life Hacks
newtab-topic-label-sports = Sports
newtab-topic-label-tech = Tech
newtab-topic-label-travel = Travel
newtab-topic-label-home = Home & Garden

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Select topics to fine-tune your feed
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Choose two or more topics. Our expert curators prioritize stories tailored to your interests. Update anytime.
newtab-topic-selection-save-button = Save
newtab-topic-selection-cancel-button = Cancel
newtab-topic-selection-button-maybe-later = Maybe later
newtab-topic-selection-privacy-link = Learn how we protect and manage data
newtab-topic-selection-button-update-interests = Update your interests
newtab-topic-selection-button-pick-interests = Pick your interests

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Follow
# Variables:
#   $topic (string) - Topic that the user can follow
newtab-section-follow-button-label =
    .aria-label = Follow { $topic }
newtab-section-following-button = Following
newtab-section-unfollow-button = Unfollow
# Variables:
#   $topic (string) - Topic that the user is following and can unfollow
newtab-section-unfollow-button-label =
    .aria-label = Following: Unfollow { $topic }
# A modal may appear next to the Follow button, directing users to try out the feature
newtab-section-follow-highlight-title = Fine-tune your feed
newtab-section-follow-highlight-subtitle = Follow your interests to see more of what you like.

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Block
newtab-section-blocked-button = Blocked
newtab-section-unblock-button = Unblock

# Variables:
#   $topic (string) - Name of topic that user is following
newtab-section-follow-topic =
    .aria-label = Follow { $topic }
# Variables:
#   $topic (string) - Name of topic that user is unfollowing
newtab-section-unfollow-topic =
    .aria-label = Unfollow { $topic }
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic =
    .aria-label = Block { $topic }
# Variables:
#   $topic (string) - Name of topic that user is unblocking
newtab-section-unblock-topic =
    .aria-label = Unblock { $topic }

## Confirmation modal for blocking a section

newtab-section-confirm-block-topic-p1 = Are you sure you want to block this topic?
newtab-section-confirm-block-topic-p2 = Blocked topics will no longer appear in your feed.

# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Block { $topic }
newtab-section-block-cancel-button = Cancel

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Topics
newtab-section-manage-topics-button-v2 =
    .label = Manage topics
newtab-section-mangage-topics-followed-topics = Followed
newtab-section-mangage-topics-followed-topics-empty-state = You have not followed any topics yet.
newtab-section-mangage-topics-blocked-topics = Blocked
newtab-section-mangage-topics-blocked-topics-empty-state = You have not blocked any topics yet.

## Strings for custom wallpaper highlight

newtab-custom-wallpaper-title = Custom wallpapers are here
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Upload your own wallpaper or pick a custom color to make { -brand-product-name } yours.
newtab-custom-wallpaper-cta = Try it

## Strings for new user activation custom wallpaper highlight

newtab-new-user-custom-wallpaper-title = Choose a wallpaper to make { -brand-product-name } yours
newtab-new-user-custom-wallpaper-subtitle = Make every new tab feel like home with custom wallpapers and colors.
newtab-new-user-custom-wallpaper-cta = Try it now

## Strings for Nova wallpaper feature highlight

newtab-wallpaper-feature-highlight-title = Fresh new wallpapers just landed
newtab-wallpaper-feature-highlight-subtitle = Choose your favorite and make every new tab feel like home.
newtab-wallpaper-feature-highlight-cta = Choose wallpaper

## Strings for download mobile highlight

newtab-download-mobile-highlight-title = Download { -brand-product-name } for mobile
# "Scan the code" refers to scanning the QR code that appears above the body text that leads to Firefox for mobile download.
newtab-download-mobile-highlight-body-variant-a = Scan the code to securely browse on the go.
newtab-download-mobile-highlight-body-variant-b = Pick up where you left off when you sync your tabs, passwords, and more.
newtab-download-mobile-highlight-body-variant-c = Did you know you can take { -brand-product-name } on the go? Same browser. In your pocket.
newtab-download-mobile-highlight-image =
    .aria-label = QR code to download { -brand-product-name } for mobile

## Strings for shortcuts highlight

newtab-shortcuts-highlight-title = Your favorites at your fingertips
newtab-shortcuts-highlight-subtitle = Add a shortcut to keep your favorite sites one click away.

## Strings for reporting issues with ads and content

newtab-report-content-why-reporting-this =
  .label = Why are you reporting this?
newtab-report-ads-reason-not-interested =
  .label = I’m not interested
newtab-report-ads-reason-inappropriate =
  .label = It’s inappropriate
newtab-report-ads-reason-seen-it-too-many-times =
  .label = I’ve seen it too many times
newtab-report-content-wrong-category =
  .label = Wrong category
newtab-report-content-outdated =
  .label = Outdated
newtab-report-content-inappropriate-offensive =
  .label = Inappropriate or offensive
newtab-report-content-spam-misleading =
  .label = Spam or misleading
newtab-report-content-requires-payment-subscription =
  .label = Requires payment or subscription
newtab-report-content-requires-payment-subscription-learn-more = Learn more
newtab-report-cancel = Cancel
newtab-report-submit = Submit
newtab-toast-thanks-for-reporting =
    .message = Thank you for reporting this.
newtab-toast-widgets-hidden =
    .message = Select the pencil icon to add widgets back anytime.
# Variables:
#   $topic (string) - Topic that the user has followed
newtab-section-toast-follow =
    .message = You’re now following { $topic }.
# Variables:
#   $topic (string) - Topic that the user has unfollowed
newtab-section-toast-unfollow =
    .message = You’re no longer following { $topic }.
# Variables:
#   $topic (string) - Topic that the user has blocked
newtab-section-toast-block =
    .message = You won’t see stories about { $topic } anymore.

## Strings for task / to-do list productivity widget

newtab-widget-section-title = Widgets
newtab-widget-menu-hide = Hide widget
newtab-widget-menu-change-size = Change size
# Parent label for a submenu in the widget menu that reorders the widget
# among its siblings. "Left" and "Right" appear as items inside this submenu.
newtab-widget-menu-move = Move
# Submenu item under "Move"; moves the widget one position to the left.
# RTL locales should translate this as "Right".
newtab-widget-menu-move-left = Left
# Submenu item under "Move"; moves the widget one position to the right.
# RTL locales should translate this as "Left".
newtab-widget-menu-move-right = Right
newtab-widget-size-small = Small
newtab-widget-size-medium = Medium
newtab-widget-size-large = Large
# Tooltip for hide all widgets button
newtab-widget-section-hide-all-button =
    .title = Hide widgets
    .aria-label = Hide all widgets
newtab-widget-section-maximize =
    .title = Expand widgets
    .aria-label = Expand all widgets to full size
newtab-widget-section-minimize =
    .title = Minimize widgets
    .aria-label = Collapse all widgets to compact size
newtab-widget-section-menu-button =
    .title = Widgets menu
    .aria-label = Open widgets menu
newtab-widget-add-widgets-button =
    .aria-label = Add widget
    .title = Add widget
newtab-widget-section-menu-manage = Manage widgets
newtab-widget-section-menu-hide-all = Hide widgets
newtab-widget-section-menu-learn-more = Learn more
newtab-widget-section-feedback = Tell us what you think
# Button shown when additional widgets are hidden beyond the
# first row, allowing users to show them.
newtab-widget-section-show-more =
    .label = Show more widgets
# Button shown when the widgets row is expanded to multiple rows,
# allowing users to collapse it back to one row.
newtab-widget-section-show-less =
    .label = Show fewer widgets
newtab-widget-lists-name-default = Checklist
newtab-widget-lists-name-label-default =
    .label = Task list
newtab-widget-lists-name-label-checklist =
    .label = Checklist
newtab-widget-lists-name-placeholder-default =
    .placeholder = Task list
newtab-widget-lists-name-placeholder-checklist2 =
    .placeholder = Checklist
    .aria-label = Edit list name
# The placeholder value of the name field for a newly created list
newtab-widget-lists-name-placeholder-new2 =
    .placeholder = New list
    .aria-label = Edit list name

# "Add one" means adding a new task to the list (e.g., "Walk the dog")
newtab-widget-lists-empty-cta = The possibilities are endless. Add one.
# A simple label next to the default list name letting users know this is a new / beta feature
newtab-widget-lists-label-new =
    .label = New
newtab-widget-lists-label-beta =
    .label = Beta
# When tasks have been previous marked as complete, they will appear in their own separate list beneath incomplete items
# Variables:
#   $number (number) - Amount of list items marked complete
newtab-widget-lists-completed-list = Completed ({ $number })
newtab-widget-lists-celebration-headline = Good work
newtab-widget-lists-celebration-subhead = All clear
newtab-widget-task-list-menu-copy = Copy
newtab-widget-lists-menu-edit = Edit list name
newtab-widget-lists-menu-edit2 =
    .aria-label = Edit list name
newtab-widget-lists-menu-create = Create a new list
newtab-widget-lists-menu-delete = Delete this list
newtab-widget-lists-menu-copy = Copy list to clipboard
newtab-widget-lists-menu-learn-more = Learn more
newtab-widget-lists-button-add-item = Add an item
newtab-widget-lists-input-add-an-item2 =
    .placeholder = Add an item
    .aria-label = Add an item
newtab-widget-lists-input-error = Please include text to add an item.
newtab-widget-lists-input-menu-open-link = Open link
newtab-widget-lists-input-menu-move-up = Move up
newtab-widget-lists-input-menu-move-down = Move down
newtab-widget-lists-input-menu-delete = Delete
newtab-widget-lists-input-menu-edit = Edit
newtab-widget-lists-input-menu-edit2 =
    .aria-label = Edit item
newtab-widget-lists-edit-clear =
    .aria-label = Cancel
    .title = Cancel

# the + symbol emphasises the functionality of adding a new list
newtab-widget-lists-dropdown-create =
    .label = + Create a new list

## Strings for timer productivity widget
## When the timer ends, a system notification may be shown. Depending on which mode the timer is in, that message would be shown

newtab-widget-timer-notification-title = Timer
newtab-widget-timer-notification-focus = Focus time is up. Nice work. Need a break?
newtab-widget-timer-notification-break = Your break is over. Ready to focus?
newtab-widget-timer-notification-warning = Notifications are off
newtab-widget-timer-mode-focus =
    .label = Focus
newtab-widget-timer-mode-break =
    .label = Break
newtab-widget-timer-label-play =
    .label = Play
newtab-widget-timer-label-pause =
    .label = Pause
newtab-widget-timer-reset =
    .title = Reset
newtab-widget-timer-menu-notifications = Turn off notifications
newtab-widget-timer-menu-notifications-on = Turn on notifications
newtab-widget-timer-menu-learn-more = Learn more

## Sports widget

newtab-sports-widget-menu-follow-teams = Follow teams
newtab-sports-widget-menu-view-schedule = View schedule
newtab-sports-widget-menu-view-upcoming = View upcoming
newtab-sports-widget-menu-view-results = View results
# Milestone dates (e.g. group stage, semifinals, etc.). Refers to calendar dates.
newtab-sports-widget-menu-key-dates = Key dates
newtab-sports-widget-menu-learn-more = Learn more

# “Keep tabs on” is an informal expression meaning to stay updated on, stay informed on, or regularly follow something (in this case, World Cup matches and updates).
newtab-sports-widget-keep-tabs = Keep tabs on the World Cup
newtab-sports-widget-get-updates = Get live match updates and more.
newtab-sports-widget-view-matches =
    .label = View matches
newtab-sports-widget-follow-teams =
    .label = Follow teams

# Variables:
#   $number (number) - Maximum number of teams a user can choose to follow in the team selection state
newtab-sports-widget-follow-teams-title =
    { $number ->
        *[other] Follow up to { $number } teams
    }
newtab-sports-widget-choose-wallpaper =
    .label = Choose a wallpaper
newtab-sports-widget-skip = Skip
newtab-sports-widget-search-country =
    .placeholder = Search country
    .aria-label = Search country
newtab-sports-widget-cancel = Cancel
newtab-sports-widget-back-button =
    .aria-label = Back
newtab-sports-widget-done-button =
    .label = Done
# Shown in the follow-teams list for a team that has been knocked out of the tournament.
# Variables:
#   $teamName (string) - the localized team name (e.g. "Canada").
newtab-sports-widget-team-name-eliminated = { $teamName } (eliminated)
newtab-sports-widget-view-all =
    .label = View all
newtab-sports-widget-show-less =
    .label = Show less
# Toggle that filters the list of teams the user follows
newtab-sports-widget-followed-only-toggle =
    .label = Only followed teams

## Sports widget live-games pagination. Shown when 2+ matches are live at the same time

# arrow button that goes to the previous page of live matches.
newtab-sports-widget-pagination-previous =
    .aria-label = Previous
    .title = Previous
# arrow button that goes to the next page of live matches.
newtab-sports-widget-pagination-next =
    .aria-label = Next
    .title = Next
# Dot indicator that jumps directly to a given live match.
# $index (number) - 1-based position of this dot in the list.
# $total (number) - Total number of live matches.
newtab-sports-widget-pagination-dot =
    .aria-label = Live match { $index } of { $total }
    .title = Live match { $index } of { $total }

## Watch live stream dialog
## Shown when the user clicks the “Watch live” button on a live match.
## Lists available streaming services where the match can be watched.

# Watch is a verb (as in watch matches online).
newtab-sports-widget-watch =
    .label = Watch
    .title = Watch live

# Watch is a verb (as in watch matches online).
newtab-sports-widget-watch-icon =
    .aria-label = Watch live
    .title = Watch live

newtab-sports-widget-watch-dialog-close =
    .aria-label = Close
    .title = Close

# Tag: user can watch without paying (sign-in may still be required).
newtab-sports-widget-watch-stream-free = Free

# Tag: user can start watching via a trial; continued access may require payment after it ends.
newtab-sports-widget-watch-stream-free-trial = Free trial

# Tag: provider offers both a no-cost or trial path and a paid path.
newtab-sports-widget-watch-stream-free-paid = Free and paid

# Tag: user must pay to watch (subscription, TV provider, premium plan, or add-on).
newtab-sports-widget-watch-stream-paid = Paid

# Note: provider only streams some matches, not the full tournament.
newtab-sports-widget-watch-stream-select-games-only = Select games only

# Heading for the list of streaming services available in the user’s country/region.
newtab-sports-widget-watch-available-region = Available in your region

# Heading for the list of streaming services available outside the user’s country/region.
newtab-sports-widget-watch-available-other-regions = Other regions

# Button that opens the provider’s stream page in a new tab.
newtab-sports-widget-watch-play =
    .aria-label = Open stream
    .title = Open stream

##

# The "LIVE" string is meant to be uppercase in English, but other languages and locales may vary in how they handle this.
newtab-sports-widget-live = LIVE
newtab-custom-widget-live-refresh =
    .title = Refresh scores
    .aria-label = Refresh scores
# Milestone dates (e.g. group stage, semifinals, etc.). Refers to calendar dates.
newtab-sports-widget-key-dates = Key dates
newtab-sports-widget-upcoming = Upcoming
# Used for a match currently ongoing
newtab-sports-widget-now = Now
newtab-sports-widget-results = Results
newtab-sports-widget-round-32 = Round of 32
newtab-sports-widget-round-16 = Round of 16
newtab-sports-widget-quarter-finals = Quarter-finals
newtab-sports-widget-semi-finals = Semi-finals
newtab-sports-widget-bronze-finals = Bronze Final
# Final is the final match for 1st place.
newtab-sports-widget-final = Final

## Labels for the 12 World Cup group-stage groups (Group A through Group L).
## Each team is assigned to one of these groups during the World Cup tournament group stage.

newtab-sports-widget-group-stage = Group Stage
newtab-sports-widget-group-a = Group A
newtab-sports-widget-group-b = Group B
newtab-sports-widget-group-c = Group C
newtab-sports-widget-group-d = Group D
newtab-sports-widget-group-e = Group E
newtab-sports-widget-group-f = Group F
newtab-sports-widget-group-g = Group G
newtab-sports-widget-group-h = Group H
newtab-sports-widget-group-i = Group I
newtab-sports-widget-group-j = Group J
newtab-sports-widget-group-k = Group K
newtab-sports-widget-group-l = Group L

##

# Variables:
#   $start (Date) - Start date of a tournament stage
#   $end (Date) - End date of a tournament stage
newtab-sports-widget-key-date-range = { DATETIME($start, month: "short", day: "numeric") } – { DATETIME($end, month: "short", day: "numeric") }

# Variables:
#   $date (Date) - Date of a single tournament event
newtab-sports-widget-key-date = { DATETIME($date, month: "short", day: "numeric") }

newtab-sports-widget-delayed = Delayed
newtab-sports-widget-postponed = Postponed
newtab-sports-widget-suspended = Suspended
newtab-sports-widget-cancelled = Cancelled

newtab-sports-widget-information = Information about the match
newtab-sports-widget-no-live-data = Live match data isn’t updating right now
newtab-sports-widget-view-results-link = View results

newtab-sports-widget-third-place = Third place
# Runner-up is the team in 2nd place.
newtab-sports-widget-runner-up = Runner-up
newtab-sports-widget-champions = Champions
newtab-sports-widget-world-cup-champions = 2026 World Cup Champions

# Variables:
#   $date (Date) - The match start time
newtab-sports-widget-match-time = { DATETIME($date, hour: "2-digit", minute: "2-digit") }
newtab-sports-widget-match-full-time = Full time
newtab-sports-widget-match-halftime = Halftime
newtab-sports-widget-match-extra-time = Extra time
newtab-sports-widget-match-penalties = Penalties
# Separator shown between two teams in a placeholder match row when no upcoming
# match details are available yet.
newtab-sports-widget-match-vs = vs
# Note shown in the Upcoming tab when no match details are available yet.
newtab-sports-widget-no-upcoming-matches = Stay tuned for upcoming match details

## Accessible labels for match rows in the sports widget. These are read by
## screen readers to announce the match details and status.
## Variables shared by all messages in this group:
##   $homeTeam (String) - The full name of the home team (e.g. "Mexico")
##   $awayTeam (String) - The full name of the away team (e.g. "Russia")

# A finished match row (regular full-time result).
# Variables:
#   $homeScore (number) - The home team's regular-time score
#   $awayScore (number) - The away team's regular-time score
newtab-sports-widget-match-aria-label-results =
    .aria-label = { $homeTeam }, { $homeScore } versus { $awayTeam }, { $awayScore }

# A finished match row that went to a penalty shootout.
# Parenthesized values are the shootout score.
# Variables:
#   $homeScore (number) - The home team's regular-time score
#   $awayScore (number) - The away team's regular-time score
#   $homePenalty (number) - The home team's penalty shootout score
#   $awayPenalty (number) - The away team's penalty shootout score
newtab-sports-widget-match-aria-label-results-penalties =
    .aria-label = { $homeTeam }, { $homeScore } ({ $homePenalty }) versus { $awayTeam }, { $awayScore } ({ $awayPenalty })

# A match that is currently in progress.
# Variables:
#   $homeScore (number) - The home team's current score
#   $awayScore (number) - The away team's current score
newtab-sports-widget-match-aria-label-now =
    .aria-label = Live: { $homeTeam }, { $homeScore } versus { $awayTeam }, { $awayScore }

# An upcoming scheduled match row. Announces kickoff time and date.
# Variables:
#   $date (Date) - The scheduled kickoff date/time
newtab-sports-widget-match-aria-label-upcoming =
    .aria-label = { $homeTeam } vs. { $awayTeam }, { DATETIME($date, hour: "numeric", minute: "numeric") }, { DATETIME($date, day: "numeric", month: "long") }

# An upcoming match row whose status is "delayed".
newtab-sports-widget-match-aria-label-upcoming-delayed =
    .aria-label = { $homeTeam } vs. { $awayTeam }, delayed

# An upcoming match row whose status is "postponed".
newtab-sports-widget-match-aria-label-upcoming-postponed =
    .aria-label = { $homeTeam } vs. { $awayTeam }, postponed

# An upcoming match row whose status is "suspended".
newtab-sports-widget-match-aria-label-upcoming-suspended =
    .aria-label = { $homeTeam } vs. { $awayTeam }, suspended

# An upcoming match row whose status is "cancelled".
newtab-sports-widget-match-aria-label-upcoming-cancelled =
    .aria-label = { $homeTeam } vs. { $awayTeam }, cancelled

## Sports widget — team names (FIFA country codes)
## Only includes names not adequately covered by standard country-code
## internationalization tooling.

newtab-sports-widget-team-name-label-bih =
    .label = Bosnia and Herzegovina
newtab-sports-widget-team-name-label-civ =
    .label = Ivory Coast
newtab-sports-widget-team-name-label-cod =
    .label = DR Congo
newtab-sports-widget-team-name-label-eng =
    .label = England
newtab-sports-widget-team-name-label-sco =
    .label = Scotland

# Placeholder used in a match row's aria-label for an undecided team (shown visually as "--").
newtab-sports-widget-team-tbd = To be determined

## Sports widget OMC messages
## Shown as on-screen messages promoting the Sports widget and World Cup wallpapers.

newtab-sports-widget-message-wallpapers-title = Kick off the World Cup with new wallpapers
newtab-sports-widget-message-wallpapers-body = Bring some match-day energy to your browser for the tournament.
newtab-sports-widget-message-wallpapers-cta = Choose wallpaper
newtab-sports-widget-message-add-widgets-cta =
    .label = Add widgets
newtab-sports-widget-message-day-in-play-title = Keep your day in play with { -brand-product-name } widgets
newtab-sports-widget-message-day-in-play-body = Follow the World Cup, stay on task, track time around the globe, and more.
newtab-sports-widget-message-explore-widgets-cta =
    .label = Explore widgets

## Strings introduced by the Nova redesign of the Timer widget

# Variables:
#   $minutes (number) - The currently selected timer duration in minutes
newtab-widget-timer-start-aria =
    .aria-label =
        { $minutes ->
            *[other] Start { $minutes }-minute timer
        }
newtab-widget-timer-pause-aria =
    .aria-label = Pause timer
# Variables:
#   $minutes (number) - The currently selected timer duration in minutes
newtab-widget-timer-spinbutton-name =
    .aria-label =
        { $minutes ->
            [one] { $minutes } minute
            *[other] { $minutes } minutes
        }
newtab-widget-timer-decrease-min =
    .title = Decrease 1 minute
newtab-widget-timer-increase-min =
    .title = Increase 1 minute
newtab-widget-timer-mode-group =
    .aria-label = Timer mode
# Small label shown beneath the live time while the focus timer is running or paused.
newtab-widget-timer-running-focus = Focus
# Small label shown beneath the live time while the break timer is running or paused.
newtab-widget-timer-running-break = Break
# Context-menu item to hide the Timer widget. Replaces the shared "Hide widget"
# copy with a widget-specific string per the Nova design.
newtab-widget-timer-menu-hide = Hide timer
# Heading shown inside the Timer widget after a focus session ends.
newtab-widget-timer-celebration-heading-focus = Nice work
# Heading shown inside the Timer widget after a break session ends.
newtab-widget-timer-celebration-heading-break = Your break is over
# Message shown inside the Timer widget after a focus session ends.
newtab-widget-timer-celebration-message-focus = Need a break?
# Message shown inside the Timer widget after a break session ends.
newtab-widget-timer-celebration-message-break = Ready to focus?

# Strings for daily briefing card

# The title displays above a set of top news headlines.
newtab-daily-briefing-card-title = Top Headlines
newtab-daily-briefing-card-menu-dismiss = Dismiss

# Variables:
#   $minutes (number) - Time since the feed has been refreshed
newtab-daily-briefing-card-timestamp = Updated { $minutes }m ago

newtab-widget-message-title = Stay focused with lists and a built-in timer
# to-dos stands for "things to do".
newtab-widget-message-copy = From quick reminders to daily to-dos, focus sessions to stretch breaks — stay on task and on time.
# One spot refers to a dedicated section on new tab to manage and use widgets
newtab-widget-message-focus-forecasts-title = One spot for focus, forecasts, and more
newtab-widget-message-focus-forecasts-body = Keep your day flowing with { -brand-product-name } widgets. Check the forecast, stay on task, or track time across the globe.

# Strings for the default promo card

newtab-promo-card-title = Support { -brand-product-name }
newtab-promo-card-body = Our sponsors support our mission to build a better web
newtab-promo-card-cta = Learn more

# "Make Firefox yours" refers to about:newtab. The call to action here ("Try it now")
# is to customize the new tab page with a background image or color from
# the built-in wallpaper collection or uploading your own image.
newtab-promo-card-title-addons = Make { -brand-product-name } yours
newtab-promo-card-body-addons = Pick a wallpaper from our collection, or create your own.
newtab-promo-card-cta-addons = Try it now
newtab-promo-card-dismiss-button =
    .title = Dismiss
    .aria-label = Dismiss

## Strings for activation window message variants. In certain experiment configurations,
## the strings from these variants may be displayed in a message below the search input
## for the first 48 hours of a new profile's lifetime. Some messages include buttons with
## labels, but not all.

newtab-activation-window-message-dismiss-button =
    .title = Dismiss
    .aria-label = Dismiss

# "This space" refers to about:newtab. The call to action here ("make it your own")
# is to customize newtab with a background image or colour, or by tweaking the
# existing widgetry that appears on it.
newtab-activation-window-message-customization-focus-header = Make this space your own
newtab-activation-window-message-customization-focus-message = Choose a fresh wallpaper, add shortcuts to your favorite sites, and stay up-to-date on stories that interest you.
newtab-activation-window-message-customization-focus-primary-button =
    .label = Start customizing

# "This space" refers to about:newtab. The sentiment of "plays by your rules" is
# meant to evoke the idea that newtab is malleable and customizable. The call to
# action is to customize newtab with a background image or colour, or by tweaking
# the existing widgetry that appears on it.
newtab-activation-window-message-values-focus-header = This space plays by your rules
newtab-activation-window-message-values-focus-message = { -brand-product-name } lets you browse the way you like, with a more personal way to start your day online. Make { -brand-product-name } your own.

## Strings for the Clock widget

# Context menu item: toggle the clock card off.
newtab-clock-widget-menu-hide = Hide clock
newtab-clock-widget-menu-learn-more = Learn more
newtab-clock-widget-menu-edit = Edit clocks
newtab-clock-widget-menu-switch-to-12h = Switch to 12-hour format
newtab-clock-widget-menu-switch-to-24h = Switch to 24-hour format
newtab-clock-widget-label-your-clocks = Your clocks
newtab-clock-widget-search-location-input =
    .label = Location
    .placeholder = Search for a city
    .aria-label = Search for a city
# "Nickname (optional)" refers to a custom, user-defined label for a saved location
# (e.g., "Home", "Office", or "School") to make it easier to recognize.
# Not to be translated as a legal name, username, or alias used for identity verification.
newtab-clock-widget-input-nickname =
    .label = Nickname (optional)
    .placeholder = Add a nickname
    .aria-label = Nickname (optional)
# "Add new clock" is an icon-only button in the widget toolbar — the
# attributes are consumed as tooltip/screen-reader label only. The button
# never renders visible text.
newtab-clock-widget-button-add =
    .title = Add new clock
    .aria-label = Add new clock
newtab-clock-widget-button-add-clock = Add
newtab-clock-widget-button-cancel = Cancel
newtab-clock-widget-button-back =
    .title = Back
    .aria-label = Back
newtab-clock-widget-button-edit-clock =
    .title = Edit clock
    .aria-label = Edit clock
newtab-clock-widget-button-save = Save
newtab-clock-widget-button-remove-clock =
    .title = Remove clock
    .aria-label = Remove clock
# Accessible name for a clock row in the "Your clocks" management panel
# when the row has no user-provided nickname. Read aloud by screen
# readers when focus lands on the row.
# Variables:
#   $city (string) - The city name displayed in the row.
newtab-clock-widget-edit-item =
    .aria-label = { $city }
# Accessible name for a clock row when a user nickname has been set.
# Variables:
#   $city (string) - The city name displayed in the row.
#   $nickname (string) - The user-provided nickname for the row.
newtab-clock-widget-edit-item-with-nickname =
    .aria-label = { $city }, nickname: { $nickname }
newtab-clock-widget-add-clock-form =
    .aria-label = Add clock
newtab-clock-widget-edit-clock-form =
    .aria-label = Edit clock
# "Search results" is the accessible label for the listbox dropdown that appears
# below the location search field, listing matching cities as the user types.
# It means "results of the search", not "search within the results".
newtab-clock-widget-search-results =
    .aria-label = Search results
# Shown in place of the search results when the user's query does not match any
# supported city — e.g. typing a misspelled name or a place not in the IANA
# time zone list.
newtab-clock-widget-search-no-results = No matches
# "Open menu for clock" is an icon-only button in the widget toolbar — the
# attributes are consumed as tooltip/screen-reader label only. The button
# never renders visible text.
newtab-clock-widget-menu-button =
    .title = Open menu for clock
    .aria-label = Open menu for clock
# $nickname (String) - The user-defined nickname for a saved clock location (e.g., "Home", "Office").
newtab-clock-widget-label-nickname-with-value = Nickname: { $nickname }

##

newtab-card-dismiss-button =
    .title = Dismiss
    .aria-label = Dismiss

## Strings for "Homepage" and "Firefox Home" sections of about:settings#home.
## Homepage panel

home-homepage-title =
    .label = Homepage

home-homepage-new-windows =
    .label = New windows

home-homepage-new-tabs =
    .label = New tabs

# This option leads to the "Custom Homepage" subpage
home-homepage-custom-homepage-button =
    .label = Choose a specific site

## Custom URLs subpage

# Subheader on the Custom Homepage subpage. Followed by a form to enter URLs and a list of URLs already saved, if any.
home-custom-homepage-card-header =
    .label = Website address(es)

home-custom-homepage-address =
    .placeholder = Enter address
home-custom-homepage-address-button =
    .label = Add address

# Shown when no custom websites/URLs to use as a homepage have been added yet
home-custom-homepage-no-results =
    .label = No websites added yet.

home-custom-homepage-delete-address-button =
    .aria-label = Delete address
    .title = Delete address

# Further options to use when setting the home page. Two action buttons are placed in line with this prompt
# to replace the current home page with a currently open page or bookmark.
home-custom-homepage-replace-with-prompt =
    .label = Replace with

# Button that appears in-line after text "Replace with" (home-custom-homepage-replace-with-prompt)
home-custom-homepage-current-pages-button =
    .label = Current opened pages

# Button that appears in-line after text "Replace with" (home-custom-homepage-replace-with-prompt)
home-custom-homepage-bookmarks-button =
    .label = Bookmarks…

## Firefox Home content

home-prefs-content-header =
    .label = { -firefox-home-brand-name }

home-prefs-search-header2 =
    .label = Search

home-prefs-stories-header2 =
    .label = Stories
    .description = Exceptional content curated by the { -brand-product-name } family

home-prefs-widgets-header =
    .label = Widgets

# Lists is a widget on New Tab, similar to a to-do widget
home-prefs-lists-header =
    .label = Lists

# Timer is a widget on New Tab, similar to the Pomodoro timer.
home-prefs-timer-header =
    .label = Timer

# Sports is a widget on New Tab showing sports scores and schedules.
home-prefs-sports-widget-header =
    .label = Sports

# Clock is a widget on New Tab that displays time zones around the world.
home-prefs-clocks-header =
    .label = Clock

home-prefs-mission-message2 =
    .message = Our sponsors support our mission to build a better web.

home-prefs-manage-topics-link2 =
    .label = Manage topics

home-prefs-choose-wallpaper-link2 =
    .label = Choose a wallpaper

home-prefs-firefox-logo-header =
    .label = { -brand-short-name } logo

# Informational message bar that appears in the Firefox Home section when the options are disabled.
# The user must select Firefox Home as their homepage for either new tabs or new windows to enable
# the features in settings.
home-prefs-firefox-home-disabled-notice =
    .message = To use these features, set new tabs or new windows to { -firefox-home-brand-name }.

# Variables:
#   $num (number) - Number of rows displayed
home-prefs-sections-rows-option-srd =
    .label =
        { $num ->
            [one] { $num } row
           *[other] { $num } rows
        }

# Dropdown option shown when an extension replaces the contents of new windows or tabs.
# Variables:
#   $extension (string) - Name of the extension
home-prefs-homepage-extension-option =
    .label = Extension ({ $extension })

home-restore-defaults-srd =
    .label = Restore Defaults
    .accesskey = R

home-mode-choice-default-fx-srd =
    .label = { -firefox-home-brand-name } (Default)

home-mode-choice-custom-srd =
    .label = Custom URLs…

home-mode-choice-blank-srd =
    .label = Blank Page

home-prefs-shortcuts-header-srd =
    .label = Shortcuts

home-prefs-shortcuts-select =
    .aria-label = Shortcuts

home-prefs-shortcuts-by-option-sponsored-srd =
    .label = Sponsored shortcuts

home-prefs-recommended-by-option-sponsored-stories-srd =
    .label = Sponsored stories

home-prefs-highlights-option-visited-pages-srd =
    .label = Visited pages

home-prefs-highlights-options-bookmarks-srd =
    .label = Bookmarks

home-prefs-highlights-option-most-recent-download-srd =
    .label = Most recent download

home-prefs-recent-activity-header-srd =
    .label = Recent activity

home-prefs-recent-activity-select =
    .aria-label = Recent activity

home-prefs-weather-header-srd =
    .label = Weather

home-prefs-support-firefox-header-srd =
    .label = Support { -brand-product-name }

home-prefs-mission-message-learn-more-link-srd = Find out how
