from iot import display, network, scheduler, system


IMAGE_DOWNLOADS = (
    {
        "url": "https://i.pinimg.com/736x/4a/5d/eb/4a5debe0dda9b15bf6aa739093147da2.jpg",
        "sha256": "b1c542a326363ff907264b3452f6e7729a55011540da7240d870060fe29a1a71",
        "background_mode": "center",
        "scale_percent": 100,
    },
    {
        "url": "https://m.media-amazon.com/images/M/MV5BMTg1NzkyNDk4N15BMl5BanBnXkFtZTgwMDE2MDIyMDE@._V1_.jpg",
        "sha256": "f73137baa64dda2ed9c218a69e31093da61bb73e27208af5ebf47c11a89d1af4",
        "background_mode": "center",
        "scale_percent": 100,
    },
    {
        "url": "https://w0.peakpx.com/wallpaper/1014/327/HD-wallpaper-disney-characters-background-disney.jpg",
        "sha256": "5423713cda73722feac3d220af137c1abc5df7072569caebb520ec9bff3060a9",
        "background_mode": "center",
        "scale_percent": 100,
    },
    {
        "url": "https://cdng.europosters.eu/pod_public/1300/279730.jpg",
        "sha256": "ca2909a0e7553eb475f74944093ad5c0a4eb5c8bb8e8a1459ae3d6bf7432d911",
        "background_mode": "center",
        "scale_percent": 50,
    },
    {
        "url": "https://cdn02.plentyone.com/epz0zx1qug71/item/images/173941/full/XXL-Fototapeten-Disney-Winnie-Puuh---Papier-Vlies-.jpg",
        "sha256": "ad1ccaebd3d6cd8c07cc9c33b8b3a61311b7d7895a3d8f74d8ecdbc7bfe323cf",
        "background_mode": "center",
        "scale_percent": 50,
    },
)


display.clear(color=(8, 13, 22))
screen_width, screen_height = display.size()
image_number = 0

title_text_box = display.draw_text_box(
    x=40,
    y=40,
    width=screen_width - 80,
    height=80,
    text="Downloaded JPEG gallery",
    background_color=(8, 13, 22),
    background_opacity=210,
    border_width=0,
    font_size=24,
)

status_text_box = display.draw_text_box(
    x=40,
    y=screen_height - 100,
    width=screen_width - 80,
    height=60,
    text="Preparing image downloads...",
    background_color=(8, 13, 22),
    background_opacity=210,
    border_width=0,
    font_size=20,
)


# Drawing continues on the render thread while Python waits for each download.
downloaded_images = []
for download_number, image_download in enumerate(IMAGE_DOWNLOADS):
    display.update_text_box(
        status_text_box,
        "Downloading image %d of %d..." % (download_number + 1, len(IMAGE_DOWNLOADS)),
    )
    downloaded_file = network.download_file(
        image_download["url"],
        expected_sha256=image_download["sha256"],
    )
    downloaded_images.append(
        {
            "path": downloaded_file["path"],
            "background_mode": image_download["background_mode"],
            "scale_percent": image_download["scale_percent"],
        }
    )


def show_next_image():
    global image_number

    selected_image = downloaded_images[image_number]
    display.set_background_image(
        selected_image["path"],
        mode=selected_image["background_mode"],
        scale_percent=selected_image["scale_percent"],
    )
    display.update_text_box(
        status_text_box,
        "Image %d of %d  |  %s, maximum scale %d%%  |  %s"
        % (
            image_number + 1,
            len(downloaded_images),
            selected_image["background_mode"],
            selected_image["scale_percent"],
            system.current_time(),
        ),
    )
    image_number = (image_number + 1) % len(downloaded_images)


show_next_image()
scheduler.every(milliseconds=5000, callback=show_next_image)
