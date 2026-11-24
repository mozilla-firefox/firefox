import pytest

URL = "https://www.knixteen.ca/products/period-swim-square-neck-one-piece?variant=51874765668638"
IMAGE_CSS = "#ProductImages .swiper-slide.swiper-slide-active[js-product-media][js-carousel-slide]"


async def does_image_move(client):
    await client.navigate(URL)
    await client.stall(2)
    return client.execute_async_script(
        """
            const [image, done] = arguments;
            let count = 0;
            let lastHeight = image.clientHeight;
            setInterval(() => {
                if (image.clientHeight != lastHeight) {
                    done(true);
                } else if (++count == 50) {
                    done(false);
                }
            }, 100);
        """,
        client.await_css(IMAGE_CSS, is_displayed=True),
    )


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    assert not await does_image_move(client)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    assert await does_image_move(client)
